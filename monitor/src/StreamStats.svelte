<script>
  export let labels = []
  export let data = {}
</script>

<div class="container">
  {#each labels as label}
    <div class="entry">
      <div class="label">{label.label}:</div>
      <div>{data[label.name]}</div>
    </div>
  {/each}
</div>

<style>
  .container {
    display: flex;
    flex-direction: column;
    justify-content: center;
    font-size: 16px;
    margin-left: 50px;
  }

  .entry {
    margin: 13px 25px 13px 0px;
    display: flex;
  }

  .label {
    flex: 1;
    margin-right: 10px;
  }
</style>
