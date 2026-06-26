import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  base: "/",
  build: {
    outDir: "build",
    assetsDir: "static",
  },
  server: {
    proxy: {
      "/api": {
        target: "http://192.168.88.25",
        changeOrigin: true,
      },
    },
  },
});
