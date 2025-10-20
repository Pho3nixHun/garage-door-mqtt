import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

const repositoryName = process.env.GITHUB_REPOSITORY?.split('/').pop();
const base = repositoryName ? `/${repositoryName}/` : '/';

export default defineConfig({
  base,
  plugins: [svelte()]
});

