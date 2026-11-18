# FFNY Modding Reference

How to prepare your own fighters, voices and maps for FFNY Studio. FFNY Studio does the conversion into the game's
formats for you. This page only covers what your files should look like before you import them.

## Fighters

### Your model

- **File types:** FBX, glTF/GLB or OBJ. Keep the textures next to the model file, and don't pack images inside a `.glb`.
- **Rig:** you don't need to rig to the Def Jam skeleton by hand.
  - Rigged models from **Mixamo**, **Unreal** (mannequin), **Rigify** and **Character Creator / iClone** are
    mapped onto the Def Jam skeleton automatically, and their skin weights are kept.
  - Other rigs work too: Studio reads the bone names (`Pelvis`, `L_UpperArm`, `thigh.L`, `Bip01 R Forearm`, ...).
  - Without a rig, Studio rigs the model for you when you import it.
- **Pose and orientation:** T-pose or A-pose, standing on the ground (feet at height 0), facing forward.
  Y is up, and the fighter faces +Z.
- **Limits:**

| Limit | Value |
|---|---|
| Triangles | 16,000 |
| Materials | 4 |
| Draws (mesh pieces after the split per bone group) | 128 |
| Bones per vertex | 3 (Studio keeps the 3 strongest) |

  Studio shows these as checks before you can test or submit, and fixes what it can by itself (for example
  merging materials or reducing triangles).

### The Def Jam skeleton (main bones)

You never have to name your bones like this. Studio maps them for you. The table helps if you want to check what
a part of your rig turns into.

| Body part | Def Jam bone |
|---|---|
| Pelvis / hips | `hip` |
| Lower spine | `waist` |
| Upper spine / chest | `chest` |
| Neck | `neck` |
| Head | `head` |
| Collarbone | `L_shoulder`, `R_shoulder` |
| Upper arm | `L_arm1`, `R_arm1` |
| Forearm | `L_arm2`, `R_arm2` |
| Hand | `L_hand`, `R_hand` |
| Fingers | `L_Thumb1-3`, `L_pointer1-3`, `L_middle1-3`, `L_ring1-3`, `L_pinky1-3` (same with `R_`) |
| Thigh | `L_leg1`, `R_leg1` |
| Shin | `L_leg2`, `R_leg2` |
| Foot and toes | `L_foot`, `R_foot` |

Twist bones, extra spine bones, eyes, jaws and props are fine: their weight goes to the nearest mapped bone above them.

### Name and voice

- **Name in the game:** at most 11 characters. Letters, digits, spaces and `' . , ! ? : # -` are allowed, and it
  can't be the name of a standard fighter.
- **Voice lines:** WAV, MP3, OGG or FLAC (any sample rate, mono or stereo). Studio converts them.

## Maps (venues)

Build the map in Blender and import it with **Import a scene** in Studio. Name your objects like this:

| In Blender | Becomes |
|---|---|
| `COL_wall` | walls fighters slam into |
| `COL_barrier` | walls without the slam |
| `COL_rope` | the ropes of a ring |
| `COL_floor` | the ground fighters stand on (`COL_drop` for a drop) |
| `START_P1`, `START_P2` (empties) | where the two fighters start |
| `CROWD_...` (empties) | crowd members |
| any other mesh | part of the map you see (props, buildings, ...) |

- A new map needs walls and a floor.
- Keep texture files next to the scene file.
- **Name in the game:** at most 24 characters.
- Crowd members must stand outside the fight area, or Studio won't start the test fight. When you make the arena
  bigger, Studio moves the crowd behind the walls for you. A coming Studio beta adds this for imported maps too:
  you can move the crowd and the start points and add objects in the map editor itself.

## Rules for online packs

- Online packs only **add** new fighters, skins and maps. They never change the standard ones.
- The FFNY team reviews every pack. Accepted packs go live with the next daily content update.
- Ranked, tournament and Standard rooms use the standard roster and maps only.

## Help

Ask in the FFNY Discord, or press **Report a problem** in Studio (under About). It sends us the log.
