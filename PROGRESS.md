# Worldbuilder: build progress

Overnight build log. Newest entries at the top of each section. Each feature is its own commit,
and every commit builds a fresh `Worldbuilder.exe` (repo → Actions → latest run → Artifacts).

## Done

### Climate, wind, ocean currents and biomes
- **Seasons** from the axial tilt (slider in the new **Climate** tab; try 0 for no seasons or 45+ for extreme ones).
  Land swings with the seasons more than sea, big continental interiors get cold winters, and air cools
  6.5 C per km of height.
- **Ocean currents**: warm currents on the east coasts of continents and cold ones on west coasts in the
  subtropics; a warm drift keeps mid-latitude west coasts mild (like western Europe).
- **Winds**: trade winds, westerlies and polar easterlies that shift north and south with the sun, plus
  monsoon winds that blow onto continents in summer.
- **Rainfall**: heavy along the equator and the storm tracks, dry under the subtropical highs; wind is
  traced back to the sea so interiors dry out, mountains wring out rain on their windward side and
  leave **rain shadows** behind.
- **15 biomes** (tropical rainforest, savanna, hot/cold desert, Mediterranean, grassland, temperate and
  boreal forest, tundra, ice sheets, sea ice, wetlands...). The Realistic map style now takes its colours
  from the climate instead of random noise.
- Overlays: Temperature and Rainfall (any month or the whole year), Biomes, Wind (animated-style arrows
  per month) and Ocean currents (warm/cold), with legends. Hover shows biome, temperatures and rainfall.
- After sculpting, **Update climate for new terrain** (Sculpt tab) re-runs it, so a new mountain range
  makes a new rain shadow.

**Decisions:** a simplified model rather than a full atmosphere simulation, so it runs in under a second.
Climate-dependent resources (bauxite, peat, salt, lithium) now use the real climate.

### Resources placed by geology
- 52 real resources in five groups: metals (iron, copper, gold, silver, lead, zinc, tin, tungsten, nickel,
  cobalt, chromium, platinum, bauxite, manganese, titanium, uranium, lithium, rare earths, molybdenum,
  mercury, vanadium, antimony), gems, fuels (coal, oil, gas, peat, oil shale), industrial minerals
  and building stone.
- Each has a geological recipe: e.g. copper and molybdenum above subduction zones, diamonds only in
  very old shields, tin and tungsten in collision-belt granites, coal in old sedimentary basins, oil and
  gas in young basins and offshore shelves, salt and lithium in dry basins.
- About 2,100 deposits per Earth-sized planet, each with a realistic size (most small, a few giants),
  grade and depth. Oil, gas and manganese can be offshore.
- **Resources tab**: pick a group and a resource to see where the geology suits it (coloured heatmap)
  and every deposit (diamond markers sized by tonnage). Hover a deposit for size, grade and depth.

**Decisions:** resources that depend on climate (bauxite, peat, salt) use a stand-in climate until the
climate simulation lands, then switch to the real one automatically.

### Tectonic plates and rock types
- The planet is split into tectonic plates (count and activity set in the **World** tab). Each plate is
  continental or oceanic and rotates around its own pole, like Earth's plates.
- Plate edges are classified as collisions (Himalaya-style ranges and plateaus), subduction zones
  (Andes-style coastal ranges plus ocean trenches), island arcs, rifts, mid-ocean ridges and transform faults.
  These now **shape the terrain**: mountains, trenches and ridges come from the plates.
- Hotspots create volcanic island chains trailing behind the plate's motion (like Hawaii).
- Every area gets a rock type (basalt sea floor, sedimentary basins, volcanic, granite, metamorphic,
  ancient shields) and an age in millions of years.
- **Map tab → Overlay**: Tectonic plates (with motion arrows), Rock types, Earthquake zones, each with a legend.
  Volcano markers show on those overlays and when zoomed in.
- Hovering shows plate, speed, rock type and age, and distance to the nearest plate edge.
- The side panel is now split into tabs (World, Map, Sculpt) to make room for what's coming.

**Decisions:** a 0.5-degree simulation grid (about 55 km cells) drives the big-picture systems; fine detail
is still generated as you zoom. Base noise mountains were toned down because the plates now make the big ranges.

## Next up
Resources placed by geology → climate and wind → rivers → save/load → nations and cities → trade → timeline and lore.
