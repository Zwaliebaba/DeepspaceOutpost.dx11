#ifndef SCENEMESHES_H
#define SCENEMESHES_H

// Register the game's ship geometry with the client's 3D scene renderer.
//
// Scene3D (NeuronClient) is game-agnostic: it asks a callback to build a mesh for a
// ship type on demand. This wires that callback to the legacy ship tables
// (ship_list / ship_solids) and the master palette, keeping the engine free of
// game-specific data. Call once at startup (after the device/renderer are up).
void register_scene_meshes(void);

#endif /* SCENEMESHES_H */
