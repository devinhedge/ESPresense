import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [svelte()],
  server: {
    proxy: {
      '^/json': {
        target: 'http://192.168.128.134/',
        changeOrigin: true,
        rewrite: (path) => path
      },
      '^/ws': {
        target: 'ws://192.168.128.134/',
        changeOrigin: true,
        rewrite: (path) => path
      },
    }
  }
})
