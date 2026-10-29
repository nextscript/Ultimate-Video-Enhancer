import { resolve } from 'node:path';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

export default defineConfig({
    root: 'src',
    base: './',
    plugins: [react()],
    build: {
        outDir: resolve(__dirname, 'dist'),
        emptyOutDir: true,
        target: 'chrome140',
        rollupOptions: { input: { index: resolve(__dirname, 'src/index.html'), export: resolve(__dirname, 'src/export.html') } }
    }
});
