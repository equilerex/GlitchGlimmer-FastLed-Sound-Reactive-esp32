# CONTEXT — visual-layers
updated: 2026-09-18

<!-- All four sections stay even when briefly empty — an absent section is
     indistinguishable from an omission.

     Does not go in: anything the code plainly shows; API or parameter docs (those
     belong in code); changelog or commit history (git has it); general framework or
     language behaviour (the model has it).
     Test: if removing a line would not slow a newcomer down, cut it. -->

## What this is

The compositor library. `VisualLayer` is the interface, `VisualLayers.h` holds the concrete classes `LayerManager::addLayerByType` constructs.

## Why it's built this way

Layers are accents on a catalog base, not scenes of their own. They sat next to the catalog headers until the two roles were indistinguishable in a folder listing.

## Gotchas

`AlienSquirtTrailLayer` is unused, is not a `VisualLayer`, and has no `LayerType`. `VisualLayers.h` is one header of many classes. That is safe because the methods are inline in the class. Pacifica is the opposite shape and does not belong here.

## Don't

Do not add catalog `Animation` subclasses here. Do not split `VisualLayers.h` just to match the folder name: the factory in `LayerManager.cpp` is the map, not the filenames.
