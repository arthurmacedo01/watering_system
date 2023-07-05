<script lang="ts">
  
  let isOn = false;

  async function onClick(on_off: boolean) {
    isOn = on_off;
    let intervalMs = document.getElementById("interval_ms").value;
    await fetch("/api/toggle-led", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ is_on: isOn , interval_ms : intervalMs}),
    });
  }
</script>



<div class="grid justify-items-center space-y-14">
  {#if isOn}
  <div on:click={() => onClick(false)} class="rounded-full border-4 border-black border-solid w-16 h-16 bg-blue-700 cursor-pointer" />
  {:else}
  <div on:click={() => onClick(true)} class="rounded-full border-4 border-black border-solid w-16 h-16  cursor-pointer" />
  {/if}
  <input id="interval_ms" type="range" min="0" max="100" value="40" class="range range-primary" />
</div>


