<script lang="ts">
  import { createEventDispatcher } from 'svelte';

  export type ButtonVariant = 'solid' | 'outline';
  export type ButtonSize = 'sm' | 'md' | 'lg';

  export let variant: ButtonVariant = 'solid';
  export let size: ButtonSize = 'md';
  export let type: 'button' | 'submit' | 'reset' = 'button';
  export let disabled = false;
  export let fullWidth = false;
  export let className = '';
  export let onClick: ((event: MouseEvent) => void) | undefined;

  const dispatch = createEventDispatcher<{ click: MouseEvent }>();

  const sizeClasses: Record<ButtonSize, string> = {
    sm: 'px-3 py-2 text-xs',
    md: 'px-4 py-3 text-sm',
    lg: 'px-6 py-4 text-lg'
  };

  const variantClasses: Record<ButtonVariant, string> = {
    solid:
      'bg-gradient-to-r from-emerald-500 via-teal-500 to-emerald-400 text-emerald-950 shadow-lg shadow-emerald-500/40 hover:from-emerald-400 hover:via-teal-400 hover:to-emerald-300 disabled:from-emerald-700/60 disabled:via-emerald-700/60 disabled:to-emerald-700/60 disabled:text-emerald-200/70',
    outline:
      'border border-emerald-200/70 text-emerald-50 bg-transparent hover:bg-emerald-400/10 hover:border-emerald-200 disabled:text-emerald-200/60 disabled:border-emerald-600/40'
  };

  const baseClasses =
    'inline-flex items-center justify-center gap-2 rounded-2xl font-semibold transition focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-emerald-200 disabled:cursor-not-allowed disabled:opacity-60';

  const handleClick = (event: MouseEvent) => {
    if (disabled) {
      event.preventDefault();
      event.stopPropagation();
      return;
    }
    onClick?.(event);
    dispatch('click', event);
  };
</script>

<button
  {...$$restProps}
  type={type}
  class={`${baseClasses} ${sizeClasses[size]} ${variantClasses[variant]} ${fullWidth ? 'w-full' : ''} ${className}`}
  disabled={disabled}
  on:click={handleClick}
>
  <slot />
</button>
