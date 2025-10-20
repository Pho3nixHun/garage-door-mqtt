import { locale, register, init, getLocaleFromNavigator, waitLocale } from 'svelte-i18n';

export const availableLanguages = [
  { code: 'en', label: 'English' },
  { code: 'de', label: 'Deutsch' },
  { code: 'hu', label: 'Magyar' },
  { code: 'ro', label: 'Română' },
  { code: 'pl', label: 'Polski' },
  { code: 'ru', label: 'Русский' },
  { code: 'uk', label: 'Українська' },
  { code: 'cs', label: 'Čeština' },
  { code: 'sk', label: 'Slovenčina' }
] as const;

export type Language = (typeof availableLanguages)[number]['code'];

const STORAGE_KEY = 'garage-door-web::language';
const FALLBACK_LOCALE: Language = 'en';

register('en', () => import('../locales/en.json').then((module) => module.default));
register('de', () => import('../locales/de.json').then((module) => module.default));
register('hu', () => import('../locales/hu.json').then((module) => module.default));
register('ro', () => import('../locales/ro.json').then((module) => module.default));
register('pl', () => import('../locales/pl.json').then((module) => module.default));
register('ru', () => import('../locales/ru.json').then((module) => module.default));
register('uk', () => import('../locales/uk.json').then((module) => module.default));
register('cs', () => import('../locales/cs.json').then((module) => module.default));
register('sk', () => import('../locales/sk.json').then((module) => module.default));

const normaliseNavigatorLocale = (): Language => {
  const navigatorLocale = getLocaleFromNavigator();
  if (!navigatorLocale) {
    return FALLBACK_LOCALE;
  }
  const match = availableLanguages.find(({ code }) => navigatorLocale.startsWith(code));
  return match?.code ?? FALLBACK_LOCALE;
};

const getStoredLocale = (): Language | null => {
  if (typeof window === 'undefined') {
    return null;
  }
  const stored = localStorage.getItem(STORAGE_KEY);
  if (!stored) {
    return null;
  }
  const match = availableLanguages.find(({ code }) => code === stored);
  return match?.code ?? null;
};

const persistLocale = (value: Language) => {
  if (typeof window === 'undefined') {
    return;
  }
  localStorage.setItem(STORAGE_KEY, value);
};

export const setupI18n = async () => {
  const stored = getStoredLocale();
  const initialLocale = stored ?? normaliseNavigatorLocale();

  init({
    fallbackLocale: FALLBACK_LOCALE,
    initialLocale
  });

  if (!stored) {
    persistLocale(initialLocale);
  }

  const unsubscribe = locale.subscribe((value) => {
    if (value) {
      persistLocale(value as Language);
    }
  });

  await waitLocale();

  return () => unsubscribe();
};

export const setLocale = async (value: Language) => {
  console.debug('[i18n] switching locale to', value);
  locale.set(value);
  await waitLocale(value);
  console.debug('[i18n] locale loaded', value);
};
