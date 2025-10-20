import './app.css';
import App from './App.svelte';
import { setupI18n } from './lib/i18n';

const target = document.getElementById('app');

if (!target) {
  throw new Error('Root element #app not found');
}

await setupI18n();

const app = new App({
  target
});

export default app;
