<!--
  Copyright 2023 Sam Johnson
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

<script>
  export let data = []
</script>

<div class="container">
  <div class="graph">
    {#each data as endpoint}
      <div class="value" style="left: {300.0 * (endpoint.lastRelativeSbn + 128) / 256.0}px;">
        <p>{endpoint.interfaceName}</p>
        <div class="dash"></div>
      </div>
    {/each}
  </div>
  <div class="markers-container">
    <div class="marker" style="left: 0px;">
      <div class="dash"></div>
      <p>-128</p>
    </div>
    <div class="marker" style="left: 150px;">
      <div class="dash"></div>
      <p>0</p>
    </div>
    <div class="marker" style="left: 299px;">
      <div class="dash"></div>
      <p>128</p>
    </div>
  </div>
</div>

<style>
  .markers-container {
    display: flex;
    margin-top: 3px;
    position: relative;
  }

  .marker {
    position: absolute;
  }

  .marker .dash {
    width: 1px;
    height: 9px;
    background-color: black;
  }

  .marker p {
    font-size: 12px;
    margin: 2px 3px 0px 0px;
    transform: translateX(-50%);
  }

  .graph {
    background-color: rgb(230, 230, 230);
    width: 300px;
    height: 40px;
    position: relative;
    display: flex;
    align-items: flex-end;
  }

  .value {
    position: absolute;
  }

  .value .dash {
    width: 1px;
    height: 15px;
    background-color: black;
  }

  .value p {
    font-size: 12px;
    margin: 2px 3px 0px 0px;
    transform: translateX(-50%);
  }
</style>
