<script>
  import "../app.css";
</script>

<div class="h-screen drawer drawer-mobile w-full">
  <input id="my-drawer-2" type="checkbox" value="true" class="drawer-toggle" />
  <div class="drawer-content flex flex-col items-center justify-center">
    <slot />
    <label for="my-drawer-2" class="btn btn-primary drawer-button lg:hidden mt-5">Menu</label>
  </div>
  <div class="drawer-side">
    <label for="my-drawer-2" class="drawer-overlay" />
    <ul class="menu p-4 overflow-y-auto w-80 bg-base-100 text-base-content">
      <!-- Sidebar content here -->
      <li><a href="/">Home</a></li>
      <li><a href="./set-interval">Configurar Intervalo</a></li>
      <li><a href="./ap-to-sta">Configurar Rede</a></li>
    </ul>
  </div>
</div>
