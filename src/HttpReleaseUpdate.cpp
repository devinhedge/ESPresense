#include "HttpReleaseUpdate.h"

#include <StreamString.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

bool isSameVersion(WiFiClientSecure& client, const String& url, const String& version) {
    HTTPClient http;
    if (!http.begin(client, url))
        return false;

    int httpCode = http.sendRequest("HEAD");
    bool ret = httpCode > 300 && httpCode < 400 && http.getLocation().indexOf(version) <= 0;

    if (ret) Serial.printf("Updating from (sc=%d): %s\n", httpCode, http.getLocation().c_str());
    else Serial.printf("Not updating from (sc=%d): %s\n", httpCode, http.getLocation().c_str());

    http.end();
    return ret;
}

HttpUpdateResult HttpReleaseUpdate::update(WiFiClientSecure& client, const String& url, const String& version) {
    if (version.length() > 0 && isSameVersion(client, url, version))
        return HTTP_UPDATE_NO_UPDATES;

    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(_httpClientTimeout);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url))
        return HTTP_UPDATE_FAILED;
    return handleUpdate(http);
}

/**
 * return error code as int
 * @return int error code
 */
int HttpReleaseUpdate::getLastError(void) {
    return _lastError;
}

/**
 * return error code as String
 * @return String error
 */
String HttpReleaseUpdate::getLastErrorString(void) {
    if (_lastError == 0) {
        return String();  // no error
    }

    // error from Update class
    if (_lastError > 0) {
        StreamString error;
        Update.printError(error);
        error.trim();  // remove line ending
        return String("Update error: ") + error;
    }

    // error from http client
    if (_lastError > -100) {
        return String("HTTP error: ") + HTTPClient::errorToString(_lastError);
    }

    switch (_lastError) {
        case HTTP_UE_TOO_LESS_SPACE:
            return "Not Enough space";
        case HTTP_UE_SERVER_NOT_REPORT_SIZE:
            return "Server Did Not Report Size";
        case HTTP_UE_SERVER_FILE_NOT_FOUND:
            return "File Not Found (404)";
        case HTTP_UE_SERVER_FORBIDDEN:
            return "Forbidden (403)";
        case HTTP_UE_SERVER_WRONG_HTTP_CODE:
            return "Wrong HTTP Code";
        case HTTP_UE_SERVER_FAULTY_MD5:
            return "Wrong MD5";
        case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
            return "Verify Bin Header Failed";
        case HTTP_UE_BIN_FOR_WRONG_FLASH:
            return "New Binary Does Not Fit Flash Size";
        case HTTP_UE_NO_PARTITION:
            return "Partition Could Not be Found";
    }

    return String();
}

HttpUpdateResult HttpReleaseUpdate::handleUpdate(HTTPClient& http) {
    HttpUpdateResult ret = HTTP_UPDATE_FAILED;
#ifdef VERSION
    http.setUserAgent("ESPresense/" VERSION);
#else
    http.setUserAgent("ESPresense/0.0");
#endif

    int code = http.GET();
    if (code <= 0) {
        Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
        _lastError = code;
        goto exit;
    }

    switch (code) {
        case HTTP_CODE_OK: {
            int len = http.getSize();
            if (len > 0) {
                int sketchFreeSpace = ESP.getFreeSketchSpace();
                if (!sketchFreeSpace) {
                    _lastError = HTTP_UE_NO_PARTITION;
                    goto exit;
                }

                if (len > sketchFreeSpace) {
                    Serial.printf("FreeSketchSpace too low (%d) needed: %d\n", sketchFreeSpace, len);
                    _lastError = HTTP_UE_TOO_LESS_SPACE;
                    goto exit;
                }

                if (_cbStart) {
                    _cbStart();
                }

                WiFiClient* tcp = http.getStreamPtr();
                if (runUpdate(*tcp, len)) {
                    ret = HTTP_UPDATE_OK;
                    if (_rebootOnUpdate) {
                        ESP.restart();
                    }

                    if (_cbEnd) {
                        _cbEnd();
                    }
                } else {
                    ret = HTTP_UPDATE_FAILED;

                    if (_cbEnd) {
                        _cbEnd();
                    }
                }
            } else {
                _lastError = HTTP_UE_SERVER_NOT_REPORT_SIZE;
                Serial.printf("Content-Length was 0 or wasn't set by Server?!\n");
                goto exit;
            }
        } break;
        case HTTP_CODE_NOT_MODIFIED:
            ret = HTTP_UPDATE_NO_UPDATES;
            break;
        case HTTP_CODE_NOT_FOUND:
            _lastError = HTTP_UE_SERVER_FILE_NOT_FOUND;
            ret = HTTP_UPDATE_FAILED;
            break;
        case HTTP_CODE_FORBIDDEN:
            _lastError = HTTP_UE_SERVER_FORBIDDEN;
            ret = HTTP_UPDATE_FAILED;
            break;
        default:
            _lastError = HTTP_UE_SERVER_WRONG_HTTP_CODE;
            Serial.printf("HTTP Code is (%d)\n", code);
            break;
    }

exit:
    http.end();
    return ret;
}

bool HttpReleaseUpdate::runUpdate(Stream& in, uint32_t size) {
    StreamString error;

    if (_cbProgress) {
        Update.onProgress(_cbProgress);
    }

    if (!Update.begin(size, U_FLASH, _ledPin, _ledOn)) {
        _lastError = Update.getError();
        Update.printError(error);
        error.trim();  // remove line ending
        Serial.printf("Update.begin failed! (%s)\n", error.c_str());
        return false;
    }

    if (_cbProgress) {
        _cbProgress(0, size);
    }

    if (Update.writeStream(in) != size) {
        _lastError = Update.getError();
        Update.printError(error);
        error.trim();  // remove line ending
        Serial.printf("Update.writeStream failed! (%s)\n", error.c_str());
        return false;
    }

    if (_cbProgress) {
        _cbProgress(size, size);
    }

    if (!Update.end()) {
        _lastError = Update.getError();
        Update.printError(error);
        error.trim();  // remove line ending
        Serial.printf("Update.end failed! (%s)\n", error.c_str());
        return false;
    }

    return true;
}
