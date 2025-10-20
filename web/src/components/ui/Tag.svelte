<script lang="ts">
  export type TagVariant = 'solid' | 'outline';
  export type TagTone = 'info' | 'success' | 'warning';

  export let variant: TagVariant = 'solid';
  export let tone: TagTone = 'info';
  export let className = '';

  const solidPalette: Record<TagTone, string> = {
    info: 'bg-sky-500/90 text-sky-950',
    success: 'bg-emerald-500/90 text-emerald-950',
    warning: 'bg-amber-400/90 text-amber-950'
  };

  const outlinePalette: Record<TagTone, string> = {
    info: 'border border-sky-400/80 text-sky-100',
    success: 'border border-emerald-300/80 text-emerald-100',
    warning: 'border border-amber-300/80 text-amber-100'
  };

  const base = 'inline-flex items-center rounded-full px-3 py-1 text-xs font-semibold uppercase tracking-wide';

  $: toneClasses = variant === 'solid' ? solidPalette[tone] : `${outlinePalette[tone]} bg-transparent`;
</script>

<span class={`${base} ${toneClasses} ${className}`}>
  <slot />
</span>
