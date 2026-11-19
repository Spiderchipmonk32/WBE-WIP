# Worldbuilder: build progress

Overnight build log. Newest entries at the top of each section. Each feature is its own commit,
and every commit builds a fresh `Worldbuilder.exe` (repo → Actions → latest run → Artifacts).

## Done

### Trade and supply chains (Trade tab)
- 25 goods: food (grain, fish, meat and wool), timber, salt, stone, clay, sand, ores (iron, copper,
  tin, base metals), gold, silver, gems, coal, oil, and crafted goods (steel, bronze, tools, weapons,
  jewellery, pottery, glass, textiles) made from inputs, e.g. steel from iron ore plus coal (or charcoal
  before the early modern era), bronze from copper plus tin.
- Cities produce from the land around them (fertility, forests, coasts) and from the mines within
  300 km, and consume according to their population. Demand changes with the **era** (Ancient,
  Medieval, Early modern, Industrial, Modern): e.g. coal and steel boom in the industrial era, and oil
  only matters from then on.
- Each good flows from the cheapest supplier along a pathfound route. Travel time depends on terrain,
  navigable rivers and lakes, sea lanes (early sailors hug the coast; sailing ships ride the winds;
  steam ignores them) and the cost of loading at ports. Cheap bulk goods stay local; luxuries travel far.
- The map shows the main routes (blue by water, brown by land, thicker = more valuable). Pick a good to
  see only its routes and its price range; pick a route to see its travel time and cargo; select a city
  to see its routes, exports and imports.
- **Add your own routes**: click two cities and the app finds the best path for the era. Saved with the world.

### Nations, cultures and cities (Nations and Cities tabs)
- **Cultures** with their own naming languages (Nordic, Latin, Slavic, Eastern, Desert, Elvish,
  Celtic, Mesoamerican), so every nation's towns sound related.
- **Generate nations, cultures and cities** in one click: nations grow from the best land, with
  borders that tend to follow mountains, big rivers and deserts. Ice, deserts and remote land stay
  unclaimed for you to fill.
- Every nation has a name, colour, government (kingdom, empire, republic, theocracy, magocracy...),
  culture and notes; edit any of them, add or delete nations, and **paint or erase territory** with a brush.
- **Political map overlay** with wavy, natural-looking borders and nation names sized to their territory.
- **Cities** are placed on the best sites (rivers, coasts, fertile flat land) with capitals and realistic
  population spreads. Click a city (map or list) to rename it, set its population or capital status,
  add notes, jump to it or delete it. **Place new city** founds one where you click, named in the local
  culture. **Show good sites** highlights the best unclaimed spots.
- Cities appear on the map by importance as you zoom in, with labels and hover cards.
- **Population density overlay** shows where people would naturally live.
- Nations, cultures, cities and territory are all saved in world files.
- Also fixed: false lakes in valleys and blocky sea floor at plate edges.

### Save, load, autosave and exports (File tab)
- **Save** your world under a name; **Load** any saved world from the list. Worlds are stored in a
  `worlds` folder next to the program as small text files: the settings and seed rebuild everything
  generated, plus your sculpting and (as they arrive) nations, cities, history and notes.
- **Autosave** every 3 minutes to `worlds/autosave.world` (can be turned off).
- **Exports** (to an `exports` folder): a screenshot of the current view, the **whole world map** as an
  8192 x 4096 PNG in the current style and overlay (with rivers), a **4096 x 2048 16-bit heightmap**
  (RAW plus a PNG preview and a text file with the height range, for game engines), and every
  **resource deposit as CSV**.
- The tab bar now shows all planned sections; Nations, Cities, Trade, History and Lore say
  "coming soon" until they're built.

### Rivers and lakes
- Water drains from every point to the sea: basins fill up into **lakes** until they spill over, and
  in dry regions they evaporate into **salt flats** instead.
- **River flow** builds up from rainfall minus evaporation, so rivers grow downstream and dry regions
  have few. On an Earth-like planet the biggest rivers carry around 15,000 m3/s (Mississippi-sized).
- Rivers are drawn as smooth, gently meandering lines whose width follows their flow; small rivers
  appear as you zoom in. Toggle them in the Map tab. Lakes get natural shorelines from the detailed
  terrain. Hover shows the river flow, lake or salt flat.
- Rivers and lakes update when you press **Update climate for new terrain** after sculpting.

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
