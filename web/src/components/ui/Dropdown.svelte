<script lang="ts">
  import { onMount, tick } from "svelte";

  export let align: 'left' | 'right' = 'right';
  export let gap = '0.5rem';
  export let className = '';

  let open = false;
  let root: HTMLElement | null = null;
  let panel: HTMLElement | null = null;
  let openUp = false;

  const updatePosition = () => {
    if (!root || !panel) {
      return;
    }
    const triggerRect = root.getBoundingClientRect();
    const panelHeight = panel.offsetHeight;
    const spaceBelow = window.innerHeight - triggerRect.bottom;
    const spaceAbove = triggerRect.top;
    openUp = spaceBelow < panelHeight && spaceAbove > spaceBelow;
  };

  const toggle = async () => {
    open = !open;
    if (open) {
      await tick();
      updatePosition();
    }
  };
  const close = () => {
    open = false;
  };

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
    const handleResize = () => {
      if (open) {
        updatePosition();
      }
    };
    document.addEventListener("click", handleDocumentClick);
    window.addEventListener("resize", handleResize);
    window.addEventListener("orientationchange", handleResize);

    return () => {
      document.removeEventListener("click", handleDocumentClick);
      window.removeEventListener("resize", handleResize);
      window.removeEventListener("orientationchange", handleResize);
    };
  });
</script>

<div class={`relative inline-block ${className}`} bind:this={root}>
  <div on:click={handleTriggerClick}>
    <slot name="trigger" {toggle} {open} {close} />
  </div>
  {#if open}
    <div
      class="dropdown-panel absolute z-50"
      style={`${align === 'right' ? 'right:0;' : 'left:0;'} ${openUp ? `bottom: calc(100% + ${gap});` : `top: calc(100% + ${gap});`}`}
      bind:this={panel}
    >
      <div class="min-w-[12rem] rounded-2xl border border-emerald-300/40 bg-emerald-950/20 p-2 shadow-xl shadow-emerald-900/40 backdrop-blur">
        <slot {close} />
      </div>
    </div>
  {/if}
</div>
