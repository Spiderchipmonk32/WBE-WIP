# Worldbuilder: build progress

Overnight build log. Newest entries at the top of each section. Each feature is its own commit,
and every commit builds a fresh `Worldbuilder.exe` (repo → Actions → latest run → Artifacts).

## Done
- **Living world** (clock at the bottom, History tab): pause, 1x-100x, and 1x meaning real time, a day a second or a month a second. Keeps going while the app is closed (the last world opens at start and catches up). People are born, marry, have children, trade, steal, scheme and die; rulers are succeeded or overthrown; rivalries turn into wars that move borders and take cities; unhappy realms rebel; empty realms fall. Important events go into the timeline and a news feed.
- **Follow anyone**: a ruler, merchant, criminal or ordinary person (Cities tab, Characters tab, or the faces around a city when zoomed in). The camera rides along and their story carries on with their heir.
- **Characters**: portraits, roles, traits, family, home, faith, looks, backstory, life story, tabletop stat blocks; generate rulers, nobles and townsfolk for every nation.
- **Relationship web** (Web tab): nations, rulers and characters (plus cities, faiths, cultures) as a live graph; focus on anyone, drag to arrange, Shift-drag to link. **Family tree** view.
- **City editor**: realistic packed medieval blocks, market square, castle, temple, wall towers, fields, windmills, trees, ships; Inkarnate-style tools: select/move/rotate/resize/copy/delete, stamp 15 kinds of object, scatter, erase, paint the ground, roads, labels, undo, save image.
- **Codex** (Lore tab): 22 article templates with infoboxes, tags, secrets, [[links]] and backlinks, shown as a page.
- **Timeline view** with named ages, events, nations and their rulers, and people's lives. **Calendar** with your own month names and month length.
- **Languages**: each culture has its own tongue with a lexicon and a translator; save it to the codex.
- **Rivers** wind naturally and end at the real coast; **cities** can no longer sit in water.
- **Move tectonic plates** (World tab > Move plates): drag a plate's dot to move it, drag its arrow to change direction and speed, set it continental or oceanic, or merge it away. Drift time opens new oceans where plates part and raises ranges where they meet.
- **Better brushes** (Sculpt tab): Draw land/sea shape (lasso a continent and it fills with natural coastline, shelf and drainage), Make land/sea only raise/lower past a chosen height, Mountains and Hills brushes, edge and roughness sliders, Shift reverses.
- **City details**: what a city sells and buys (with tonnes and main partners), trade balance, what it makes, climate, rivers/ports and nearby resources; a card beside the selected city on the map.
- **Realistic populations by era**: cities sized from each nation's farmland, the era's town-dwelling share and a rank-size rule (ancient capitals ~100k, medieval up to ~0.5M, modern megacities 20-35M).
- **Editable trade routes**: draw routes through waypoints (follow terrain or straight lines), drag ends/waypoints, add/remove points, edit cargo, rename, reverse, delete; turn simulated routes into your own or hide them. Travel days per leg.
- **Wind and current flow**: animated streaks over any layer with a month picker; wind and current shown on hover.
- **New look**: Fira Sans interface font and serif map labels, dark theme, icon rail, scrolling panel with headings, layer buttons along the top of the map, status bar hints, getting-started guide.
- **Glacial fjords**: on cold coasts, big glacier-fed valleys are carved below sea level so the sea floods them.
- **Globe minimap**: tick "Globe minimap" in the Map tab to see a shaded globe centred on where you're looking, with your view circled.
- **Rotated nation labels**: long, thin nations get their names tilted and spaced out along their shape (can be turned off in the Map tab).

### Fantasy resource editor (Resources tab → Fantasy)
- Invent your own resources (mithril, mana crystals, dragonglass...): name, colour, description, and the
  rules for where they form: allowed rock types, which kind of plate edge they sit near, rock age,
  height range, temperature and rainfall ranges, how many deposits and how big.
- **Place deposits** puts them on the map alongside the real resources (existing deposits don't move),
  with the same heatmap, markers and hover details. Saved with the world and kept when you generate a new planet.

### Import images
- **Drag a PNG/JPG onto the window** and choose:
  - **The land shape**: the image (a whole-world map, twice as wide as tall; dark = sea, bright = high
    land) becomes the planet. Ocean cover is set from the dark area so the coast follows your drawing;
    noise adds natural coastlines and mountains, and plates, climate, rivers and the rest are built on it.
    The image path is saved with the world. "Stop using the image" in the World tab goes back to random land.
  - **A tracing layer**: shown over the map with adjustable transparency (Map tab), to copy an existing map.
- You can also drop a `.world` file onto the window to open it.

### Erosion
- New **Erosion** slider (World tab). Rivers carve valleys (deeper for bigger rivers) and time wears
  mountains down according to the age of their rock: ancient ranges become rolling forested hills,
  young collision ranges stay jagged and snowy. Rivers and lakes are recalculated on the eroded land.
- Salt flats now have soft, ragged edges instead of square ones.

### More sculpting, diplomacy, living routes, storms, bookmarks and exports
- **Sculpt tab**: *Make land* and *Make sea* brushes (paint coastlines directly), and one-click stamps
  for an **impact crater**, a **volcano** and a **mesa** (size = brush radius).
- **Populations grow over time**: scrub the timeline back and cities shrink towards their founding
  (markers and tooltips show the population in that year).
- **Diplomacy**: alliances, trade pacts, rivalries, wars and vassals between neighbours are generated
  with the nations; add or change them from a nation's card (pick the type, then click the other nation).
  Shown as coloured lines between capitals on the political map.
- **Caravans and ships** move along the main trade routes.
- **Storm seasons** overlay: hurricane/typhoon zones over warm seas for each month (and their landfall
  coasts), plus winter storm tracks.
- **Bookmarks** (Map tab): save views and jump back to them.
- **Exports**: the current view in high resolution (3x, sharper tiles), and a **chronicle** in Markdown
  with every nation, ruler, religion, timeline event and lore article.

### City editor (Cities tab → select a city → Open city editor)
- A separate screen for one city, showing its **real local terrain** in high detail: coastline,
  rivers (at their true width), lakes and hills, all taken from the world.
- A generated **street plan** sized to the population: main roads that bend around water and bridge
  rivers, side streets and alleys (denser towards the centre), and **city walls** with gates for
  pre-industrial eras.
- **Districts**: market, old town, noble quarter, temple district, docks (industry in later eras),
  residential, slums outside the walls, farms, parks.
- **Buildings** (up to ~30,000 for a big city) along the streets, sized and coloured by district.
- **Landmarks** named automatically: market, castle or royal palace (capital), great temple of the
  local religion, harbour, gates by compass direction, guildhall, university, an inn.
- **Edit it**: paint districts with a brush, draw your own roads, add, move, rename or delete
  landmarks, toggle walls, or roll a new street layout. All changes are saved with the world.

### Statistics, hex grid and help
- **World statistics** (World tab button): land, heights, counts of everything, a chart of land by
  biome, and the largest nations by population and area.
- **Hex grid** (Map tab) with an adjustable hex size from about 3 km to 1,000 km, anchored to the
  world, for tabletop campaigns. Combine it with the screenshot export.
- **F1** shows a help screen with every control and what each tab does.

### Religions, rulers and the Peoples tab
- **Religions** are founded in great cities and spread outwards over land (and slowly by sea), some
  further than others. Names like "Way of the River", "Church of Letruddith" or "Cult of the Silent Star".
  Rename them, add notes, see how far they've spread. **Religions** map overlay with wavy borders.
- **Cultures** can now be renamed, recoloured, given a different naming language, and previewed with
  sample names. **Cultures** map overlay.
- **Rulers**: every nation gets a line of rulers from its founding to the present, with titles that fit
  its government (King/Queen, Emperor/Empress, Consul, High Priest/Priestess, Doge, Archmage...),
  recurring dynastic names with regnal numbers, and elected terms for republics. The nation card shows
  who rules in the year set on the timeline; the Peoples tab lists them all.
- Generating nations now also creates religions and rulers. All saved in the world file.

### Hand-drawn symbols on the fantasy map
- The Fantasy style now draws **inked mountains** (lit and shaded sides, hatching, snow caps on the
  highest), **hills**, **leafy forests** and **conifer forests** (taiga), placed by the actual terrain and
  climate. Peaks mark rugged ground rather than every high plateau, and stay a readable size at every zoom.

### Templates, global climate, map tools and flags
- **World templates** (World tab): Earth-like, archipelago, Pangaea, desert world, ice world, water
  world, hothouse. Picking one sets the sliders; press Generate planet.
- **Sea level** slider (World tab): raise it to drown coasts, lower it to expose continental shelves.
- **Global temperature** and **rainfall** controls (Climate tab) for ice ages, hothouse worlds and
  deserts; Recalculate climate applies them.
- **Minimap** (top right) showing where you are; click or drag on it to jump.
- **Find** (Map tab): type part of a name to jump to any nation, city, pinned article or event.
- **Measure distance** (Map tab): click points for distance plus travel time on foot, by horse and by ship.
- **Procedural flags** for every nation (Nations tab).
- **Compass rose** and a **map title** cartouche (Map tab checkboxes), styled for the fantasy map too.

### Timeline that changes the map (History tab)
- A **year slider** (with -100/-10/+10/+100 buttons) shows the world in any year.
- **Events** placed on the map: notes, meteor impacts, volcanic eruptions, upheavals (land rises),
  sinkings (land falls), foundings, falls, wars, treaties, disasters and magical events.
- Terrain events **really change the map** from their year on: a meteor leaves a crater with a raised
  rim and a central peak (a deep one floods), an eruption builds a volcano with a summit crater, magic
  raises a ring of land around a chasm. Set each one's size and strength. Your wizard's meteor 100
  years ago leaves a crater that appears when you scrub past that year.
- **Cities appear from their founding year** and become **ruins** after they fall.
- **Generate a history** fills the timeline: city foundings (capitals oldest), wars between neighbours
  (named, with treaties and the odd sacked city), eruptions at real volcanoes, a meteor strike, plagues
  and floods.
- Event markers with tooltips; click one to edit its title, description, year, size and strength.

### Lore (Lore tab)
- Wiki-style **articles** (place, person, faction, religion, creature, item, event, other) with a
  multi-line editor (Enter for new lines, Ctrl+V to paste, Escape to stop typing).
- **Pin articles to the map**; pins show with names, and clicking one opens the article.
- **Links**: write `[[Another Article]]` and a button appears to jump to it (creating it if needed).
- Search across titles and text.
- Events, articles and city foundings/falls are all saved in the world file.

Also: smoother sampling of the tectonic uplift removes faint square patterns in shading; Escape no
longer closes the app; the WASD keys don't move the map while you're typing.

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
Core list is done. Working through the rest of the feature list (symbols, measuring, templates, more).
