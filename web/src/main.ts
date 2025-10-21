import './app.css';
import App from './App.svelte';
import { setupI18n } from './lib/i18n';

const target = document.getElementById('app');

if (!target) {
  throw new Error('Root element #app not found');
}

const bootstrap = async () => {
  await setupI18n();

  const app = new App({
    target
  });

  return app;
};

export default bootstrap();


const registerServiceWorker = () => {
  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker
        .register('/service-worker.js')
        .catch((error) => console.error('[SW] registration failed', error));
    });
  }
};

registerServiceWorker();
