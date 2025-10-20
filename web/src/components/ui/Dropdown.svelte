<script lang="ts">
  import { onDestroy, onMount } from "svelte";

  export let align: 'left' | 'right' = 'right';
  export let gap = '0.5rem';

  let open = false;
  let root: HTMLElement | null = null;

  const toggle = () => (open = !open);
  const close = () => (open = false);

  const handleDocumentClick = (event: MouseEvent) => {
    if (!open || !root) {
      return;
    }
    if (!root.contains(event.target as Node)) {
      close();
    }
  };

  const handleTriggerClick = (event: MouseEvent) => {
    event.stopPropagation();
    toggle();
  };

  onMount(() => {
    document.addEventListener("click", handleDocumentClick);
  });

  onDestroy(() => {
    document.removeEventListener("click", handleDocumentClick);
  });
</script>

<div class="relative inline-block" bind:this={root}>
  <div on:click={handleTriggerClick}>
    <slot name="trigger" {toggle} {open} {close} />
  </div>
  {#if open}
    <div
      class="absolute z-50"
      style={`${align === 'right' ? 'right:0;' : 'left:0;'} top: calc(100% + ${gap});`}
    >
      <div class="min-w-[12rem] rounded-2xl border border-emerald-300/40 bg-emerald-950/20 p-2 shadow-xl shadow-emerald-900/40 backdrop-blur">
        <slot {close} />
      </div>
    </div>
  {/if}
</div>
