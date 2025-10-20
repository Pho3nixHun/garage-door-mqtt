<script lang="ts">
  export let className = '';
</script>

<div class={`ui-card mx-auto w-full max-w-xl rounded-3xl border border-emerald-300/40 bg-emerald-950/20 text-emerald-100 shadow-xl shadow-emerald-900/30 backdrop-blur ${className}`}>
  {#if $$slots.header}
    <div class="px-6 pt-6 pb-3 space-y-2">
      <slot name="header" />
    </div>
  {/if}
  {#if $$slots.content}
    <div class="px-6 py-4 space-y-4">
      <slot name="content" />
    </div>
  {/if}
  {#if $$slots.footer}
    <div class="px-6 pb-6 pt-3">
      <slot name="footer" />
    </div>
  {/if}
</div>

