<script lang="ts">

  import { onMount } from "svelte";

  let interval_fb = 0;
  let remaining_fb = 0;
  let intensity_fb = 0;

  onMount(() => {
    updateIntervalIntensity()
    var socket = new WebSocket("ws://alimentador.local/ws");
    socket.onopen = () => socket.send("A connectin was opened");
    socket.onmessage = (event) => {
      const data = JSON.parse(event.data);
      console.log(data);
      interval_fb = data.interval;
      remaining_fb = data.remaining;
      intensity_fb = data.intensity;
    };
  });

  let remaining = 0;
  let interval = 0;
  let intensity = 0;
  
  async function onClick() {
    let remaining = document.getElementById("remaining").value;
    let interval = document.getElementById("interval").value;
    let intensity = document.getElementById("intensity").value;
    
    await fetch("/api/set-interval", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({remaining: remaining, interval : interval, intensity: intensity}),
    });
  }
function updateIntervalIntensity() {
  remaining = document.getElementById("remaining").value;
  interval = document.getElementById("interval").value;
  intensity = document.getElementById("intensity").value;
}
</script>


  <h1>Ajuste quantas horas até a próxima alimentação</h1>
  <input on:change={() => updateIntervalIntensity()} id="remaining" type="range" min="1" max="99" value="40" class="range range-primary" />
  <span class="countdown font-mono text-6xl">
  <span style={`--value:${remaining};`} />
  </span>
  
  <h1>Ajuste o intervalo em horas</h1>
  <input on:change={() => updateIntervalIntensity()} id="interval" type="range" min="1" max="99" value="40" class="range range-primary" />
  <span class="countdown font-mono text-6xl">
  <span style={`--value:${interval};`} />
  </span>
  
  <h1>Ajuste a itensidade (%)</h1>
  <input on:change={() => updateIntervalIntensity()} id="intensity" type="range" min="0" max="99" value="40" class="range range-primary" />
  <span class="countdown font-mono text-6xl">
  <span style={`--value:${intensity};`} />
  </span>

  <button on:click={() => onClick(true)} class="btn">Configurar</button>



Horas restantes até alimentar novamente:
<span class="countdown font-mono text-6xl">
  <span style={`--value:${remaining_fb};`} />
</span>
Intervalo configurado:
<span class="countdown font-mono text-6xl">
  <span style={`--value:${interval_fb};`} />
</span>
Intensidade configurada:
<span class="countdown font-mono text-6xl">
  <span style={`--value:${intensity_fb};`} />
</span>



