<script lang="ts">
  import { onMount } from "svelte";

  let isOn = false;
  let intervalMs = 0;

  onMount(() => {
    update()
  });

  async function onClick(on_off: boolean) {
    isOn = on_off;
    let intervalMs = document.getElementById("interval_ms").value;
    await fetch("/api/toggle-led", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ is_on: isOn , interval_ms : intervalMs}),
    });
  }

  function update() {
    intervalMs = document.getElementById("interval_ms").value;
  }

</script>



  <h1>Bem Vindo ao Alimentador  </h1>
  <h1>Ajuste a intensidade</h1>
  <input on:change={() => update()} id="interval_ms" type="range" min="0" max="99" value="40" class="range range-primary" />
  <span class="countdown font-mono text-6xl">
  <span style={`--value:${intervalMs};`} />
  </span>
  
  <button on:click={() => onClick(true)} class="btn gap-2">
  <svg xmlns="http://www.w3.org/2000/svg" class="h-6 w-6" fill="none" viewBox="0 0 24 24" stroke="currentColor"
    ><path
      stroke-linecap="round"
      stroke-linejoin="round"
      stroke-width="2"
      d="M4.318 6.318a4.5 4.5 0 000 6.364L12 20.364l7.682-7.682a4.5 4.5 0 00-6.364-6.364L12 7.636l-1.318-1.318a4.5 4.5 0 00-6.364 0z"
    /></svg
  >
  Alimentar
</button>


