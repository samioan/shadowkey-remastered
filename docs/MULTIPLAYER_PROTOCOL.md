# The multiplayer protocol

M128. Every message the shipped game's Bluetooth multiplayer can send or
receive, decoded from `6r51.app`: who sends it, what is on the wire, and
what the receiver does with it. This is the catalogue the port's
multiplayer is built against. **M129 implemented it** (`port/src/net/`,
and the "What the port implements" section at the end); see
`PORT_ROADMAP.md`'s M129 entry.

Addresses are `6r51_code.bin` (base `0x10000000`). Offsets on the engine
object (`CMap`, `docs/WORLD_MODEL.md`) are written `engine+0x...`.

## What it is

- **Two players, host and client.** The comms layer has two peer slots
  (0 = host, 1 = client), and both of its loops (`FUN_1000db5c` relay and
  `FUN_1000dc3c` connected count) run `for (i = 1; i < 2; ...)`. The
  avatar table is indices 0-1, tag 0x01 rejects any index >= 2, and the
  game-start packet (0x00) is 0x88 bytes: a 6-byte header and two 0x40
  names. `hostgamemenu.s`'s four "Reject" rows and `joingamemenu.s`'s four
  host slots are menu layout the netcode never backed.
- **Co-op through the campaign.** Each player brings a character from
  their own save (string 4022, "Use character from saved game?"). The
  client adopts the **host's** story state on joining (tag 0x29: the
  level record, all three quest arrays and the 18 `saved_*` ints).
- **Both handsets run the whole game.** Nothing is streamed per frame
  except each player's own movement. Everything else is an *event*,
  applied on the other handset by its own copy of the engine: a creature
  died, a door opened, an item was taken, a script variable changed.
  Host-only work is gated in the scripts themselves
  (`IsMultiplayerClient()`, e.g. crypt1.s's Umbra spawn).
- **Star topology with a relaying host.** The receive dispatcher
  (`FUN_10036b1c`) has the host forward every packet it gets to the other
  peers (`FUN_1000db5c`) before handling it, except tag 0x32 (a targeted
  hand-over). With two players the relay never has anyone to forward to,
  but the code is general.

## The objects

| What | Where | Notes |
|---|---|---|
| Session object | `engine+0x5d0`, class vtable `0x100fde1c`, ctor `FUN_1003d5e0` | 116 vtable slots; the senders and receivers are slots 0x60..0x1d4. `this+8` is the engine. |
| Comms wrapper | `engine+0x5cc` | `+0x14` the GAMECOMMS handle, `+0x34` **my player index**, `+0x38 + i*4` avatar of player i, `+0x44 + i*4` peer state (2 = connected), `+0x4a8 + i*0x208` player i's name. |
| In multiplayer | `engine+0x5c0` | Set by tag 0x00 on the client. Gates most senders. |
| Level loaded / sync allowed | `engine+0x5c1` | Gates 0x01 avatar creation and the host's 0x2e handling. |
| Pause lock | `engine+0x14cc3` | Index of the player holding a modal screen, 0xff for none (tag 0x15). |
| Stream buffer | session `+0x58` (object), `+0x60` data, `+0x64` length | The serializer behind the variable-length tags 0x1d / 0x29 / 0x32. |

GAMECOMMS (the N-Gage library, `docs/IMPORT_NAMES.md`): ordinal 32 sends
(`FUN_1000dc08(comms, buf, len)`), 33 sends to one peer (`FUN_1000db5c`),
9 returns the role (2 = host, `FUN_1000db04`).

## Framing

One message is one GAMECOMMS packet. Byte 0 is the **tag**. Fixed-size
messages are a little-endian struct padded to 4 bytes (most fields are at
+4, +8, +0xc). Variable-size ones (0x1d, 0x29, 0x32) are written with the
session's stream object: tag byte, then typed fields.

Entities are named on the wire by their **entity id** (`entity+0xc4`, a
u16, looked up with `FUN_1001a438`). Players are named by **player index**
(0 or 1, looked up with `FUN_1000dad0`, which returns the local player for
your own index and the remote avatar otherwise).

## The catalogue

"Tx" is the sender (and the game code that calls it), "Rx" the receiver.
Sizes are bytes on the wire. 0x20 is unused: its receiver slot holds no
function and the dispatcher has no case for it.

### Session

| Tag | Size | Tx | Rx | Meaning |
|---|---|---|---|---|
| 0x00 | 0x88 | `FUN_10037188` (session +0x24), host | `FUN_10038008`, client | **Game start.** +4 level index (`FUN_10039ca4`), +5 player count - 1, +6 0x40-byte names. Client stores the names, sets `engine+0x5c0`, loads the level (controller vt+0x2c), screen mode 3. |
| 0x06 | 0x10 | `FUN_1003d308`(x, y, z, heading), host (session tick, after the entity stream) | `FUN_10038674`, client | **Place me and start.** Sets the local player's x/y/z/heading, zero velocity, screen mode 5; runs the zone script's `Init` when needed; replies with 0x01, 0x22, 0x23 and 0x30. |
| 0x07 | 4 | `FUN_1003d34c` (session +0x4c) | `FUN_1003c2b4` | **Ready.** Client marks itself ready; host counts them until all peers are in. |
| 0x14 | 8 | `FUN_1003ce9c`(index) | `FUN_1003c0c8` -> session +0xcc `FUN_10039f50` | **Player left.** Clears the pause lock if they held it, shows "<name> ..." (string-table 0x3c0c). |
| 0x15 | 8 | `FUN_100377bc`(flag) (session +0x60) from many UI paths | `FUN_1003b50c` | **Pause lock.** +4 sender index, +5 1 = take, 0 = release. While another player holds it, `FUN_10029cb0` does not run this player's game frame (the "Waiting for players." / "Waiting for host" strings). |
| 0x17 | 8 | (sender not located) | `FUN_1003c504` | **Your index** = +4. |
| 0x30 | 4 | `FUN_1003cb98` (session +0x140) | `FUN_1003c010` | **Loaded** ack; host counter `+0x2c`. |
| 0x3c | 4 | `FUN_1003c700`, every 0xa00 units (10 s) from the session tick | `FUN_1003bf54` (empty) | **Keepalive.** |

### Players

| Tag | Size | Tx | Rx | Meaning |
|---|---|---|---|---|
| 0x01 | 0x20 | `FUN_1003d504` (session +0xd0): every 0xc0 units (0.75 s) **only if something changed**, from the session tick; once on leaving gameplay (`FUN_1001a3d0`) | `FUN_10038278` | **Player state.** +4 index, +8 x, +0xc y, +0x10 vx, +0x14 vy, +0x18 z (s16), +0x1a vz, +0x1c heading, +0x1e pitch. Rx creates the avatar on first sight (controller vt+0x18(0xd, 0x5a)), then sets position (z + 0x80), velocity, heading, `+0xb2 = -(pitch >> 1)`, animation 5 (moving) or 8 (still). The avatar **coasts on the velocity** between packets. |
| 0x18 | 8 | `FUN_100372e4`(weapon) (session +0xd8), player attack | `FUN_1003c45c` | **Attack visual.** +4 index, +5 animation (1 melee, 2 ranged, from the item's `+0x1a7`/`+0x17a`), +6 sound (0 none, 1 -> 599, 2 -> 0x256). No damage. |
| 0x22 | 0x108 | `FUN_1003765c`(player) (session +0x108) | `FUN_100395a4` | **Player info.** +4 index, +5 class (`+0xf38`), +6 race (`+0xf3c`), +7 sex (`+0xfac`), +8 name. Rx skins the avatar: `+0xca = raceSkin[race] + (sex ? 11 : 8)` with raceSkin = {0:3, 1:2, 2:7, 3:5, 4:4, 5:1, 6:0, 7:6}; model `engine+0x6b8c` (sex 0) or `+0x6b90`. |
| 0x23 | 8 | `FUN_1003d0e0`(v) (session +0x10c) | `FUN_1003c1c0` | **Held weapon.** +4 index, +6 s16 -> avatar `+0x2c2` (from `FUN_10044324(player)`). |
| 0x24 | 0x208 | `FUN_1003d07c` (session +0x110), player death `FUN_10042cb0` | `FUN_10039758` (inline case) | **Text message**, +6 wide text, shown as a popup (colour 0xfff). |
| 0x2a | 8 | `FUN_1003ce3c`(anim) (session +0x128), cast `FUN_10046764`, death `FUN_10042cb0` | `FUN_10039a4c` | **Animation.** +4 animation, +5 index. 6 = **death**: avatar `+0x1e4 = 1`, held; nothing animates it after that. |
| 0x3d | 8 | `FUN_1003c6b4`(amount), arrow tick `FUN_10007214` | `FUN_1003bf14` | **Damage me.** The local player's stats `DoDamage(+4, 0, 0, 0)`: an arrow that hit the other player's avatar. It names no target, which only works because there is exactly one other player. |

### Creatures and objects

| Tag | Size | Tx | Rx | Meaning |
|---|---|---|---|---|
| 0x19 | 0xc | `FUN_1003d478`(id) (session +0xdc): attack, status tick, magic hit, area spell | `FUN_10038470` | **Kill.** Entity id at +8: its stats `DoDamage(5000, localPlayer, 1, 0)`. |
| 0x1a | 0x10 | `FUN_1003d434`(id, x) (session +0xe0) | `FUN_1003857c` | **Hit for 0.** `DoDamage(0, ...)`: the hurt reaction and aggro without damage. |
| 0x1b | 0xc | `FUN_1003d3c4`(id) (session +0xe4) | `FUN_1003c3b8` | **Touched / removed.** Entity vt+0x90(entity, localPlayer, 0); if true, `FUN_100711b4` takes it out of the world. |
| 0x1d | var | `FUN_1003d24c`(entity) (session +0xf0): the host's entity stream (up to 50 a frame) | `FUN_1003893c` | **Spawn entity.** Stream: tag, typeId (s16, `+0xc8`), entity id, then the **entity class's own save chain** (vt+0x130). Rx looks the type up, creates it, loads the chain (vt+0x134), places it, runs `Init`. The same per-class format as `current.sav` (M121). |
| 0x1e | 0x10 | `FUN_1003d204`(index, entity, x) (session +0xf8), AI tick `FUN_10082224` | `FUN_10038f80` (host, or a client once placed) | **Creature targets a player.** +4 entity id, +8 must be 0, +0xc player index: `target(+0x20c)` = that player, AI package `+0x2a8 = 3` (pursue). |
| 0x1f | 0xc | `FUN_1003d1c8`(id, b, open) (session +0xfc), Object natives `FUN_10061a60` | `FUN_1003908c` | **Door.** +8 id, +4 must be 0, +5 0 = open / 1 = close: rotates `+-0x4000`, plays sound 0x3f, flips the door script's state atom. |
| 0x25 | 0x1c | `FUN_1003d020` (session +0x114) | `FUN_10039858` | **Spell projectile** from an entity (+4 = 0, id +8) or an avatar (+4 = 1, index +8): `FUN_1005f0b4` with +0xc, +0x10, +0x18 (s16). |
| 0x26 | 0x28 | `FUN_1003cfac` (session +0x118) | `FUN_1003c124` | **Effect entity**: `FUN_10073528`(world, ...) -- `Level.CreateEffect`. |
| 0x27 | 0x18 | `FUN_1003cf20`(actor, a, b, c) (session +0x11c), magic hit `FUN_100458e4`, `FUN_1004720c` | `FUN_100399a8` | **Attached effect** on an entity or avatar: `FUN_10067f84`(actor, a, b, c). |
| 0x2b | 8 | `FUN_1003cdd4`(id) (session +0x12c), Object natives | `FUN_1003c050` | **Destroy object**: `FUN_1001817c`. |
| 0x2f | 8 | `FUN_1003cc14`(entity) (session +0x13c) | `FUN_1003b18c` | **Noticed**: the entity's script `OnDetect(localPlayer)`. |
| 0x31 | 0xc | `FUN_1003cb08`(container, item) (session +0x144), Player natives | `FUN_1003b5c0` | **Taken from a container.** +4 container id, +8 item (s16, `+0xc8`): removed; an emptied destroyable bag goes (`FUN_1001b484`); an open loot menu gets `UpdateMenu`. |
| 0x32 | var | `FUN_1003ca4c`(entity) (session +0x148) | `FUN_1003b85c` | **Hand over an item.** The 0x1d stream; Rx spawns it, runs `Init`, and if it is an item gives it to the local player (player vt+0x164). **Not relayed.** |
| 0x33 | 0x18 | `FUN_1003c9a0` (all) / `FUN_1003c9f4` (one), Player and Object natives | `FUN_1003bbbc` | **Set position.** +4 2 = everyone, else one entity (+8 id); +0xc x, +0x10 y, +0x14 z. "Everyone" moves every avatar and the local player there (a group teleport). One entity: vt+0x14, velocity 0, actors snapped to the ground + 0x80. |
| 0x34 | 0x88 | `FUN_1003c94c`(entity, name) (session +0x150) | `FUN_1003bd58` | **MirrorMethod**: run script method `name` (+8, wide) on entity +4. |
| 0x35 | 0xc | `FUN_1003c908`(entity, flag) (session +0x158), Actor natives `FUN_10003810` | `FUN_1003bf58` | **Invulnerable**: `+0x1e2` = flag. |
| 0x37 | 0xc | `FUN_1003cbd0`, AI dispatcher `FUN_10084924` | `FUN_10037954` | **Umbra's scripted beat**: the entity named "Umbra" gets `+0x2ee` and runs `DelayReached`. |
| 0x39 | 0x14 | `FUN_1003c814`(caster, target, x, b), cast and item paths | `FUN_1003c550` | **Area spell**: `FUN_1004720c`(caster, target, 0, b). |
| 0x3a | 0xc | `FUN_1003c798`(creature, level), magic hit | `FUN_1003c5c0` | **Set on fire**: `+0x308 = 1`, `+0x296 = level << 8`. |
| 0x3b | 8 | `FUN_1003c738`(creature), AI tick | `FUN_1003c648` | **Alerted**: `+0x309 = 1`; picks a target if it has none (`FUN_100868cc`). |

### World and story

| Tag | Size | Tx | Rx | Meaning |
|---|---|---|---|---|
| 0x1c | 0xc | `FUN_1003d384`(n) (session +0xe8) | `FUN_1003c368` | **Room event**: room `n` of the zone's `.zon` list (`engine+0x5464`, 40 x 0x84) -> world vt+0x14. |
| 0x21 | 8 | `FUN_1003d13c`(kind, quest, flag) (session +0x104) | `FUN_100393fc` | **Quest flag** on the local player: kind 0 assigned (`+0x430`), 2 completed (`+0x530`), 1 solved (`+0x630`), index +5, value +6. |
| 0x28 | 0xc | `FUN_1003cee8`(kind, value) (session +0x120) | `FUN_100394d0` | **Share XP**: kind 0 `AddExperience(value)` (stats vt[0x20], with level-ups); kind 1 adds to stats `+0x38` directly. |
| 0x29 | var | `FUN_100373b8` (session +0xf4), host when a client is ready | `FUN_10038c94` | **Story state**: the level record (world vt+0x24 / +0x28), then for i in 0..255 the three quest bytes (assigned, completed, solved), then the 18 `saved_*` ints `+0xfd8..+0x101c`. Overwrites the client's. |
| 0x2c | 0x108 | `FUN_1003cd6c`(name, v) (session +0x130) | `FUN_1003a5b0` | **Level variable**: zone-script field `name` (+4) := `"%d"` of +0x104 (logged `LevelVar: %s:%d`). |
| 0x2d | 0x108 | `FUN_1003cd04` (session +0x134), menu natives `FUN_10078de4` | `FUN_1003aaf4` | Identical to 0x2c in layout and receiver. |
| 0x2e | 0x20 | `FUN_1003cc5c` (session +0x138), `Level.LoadLevel` (`FUN_1006dbec`) | `FUN_1003b038`, host only | **Travel.** +4 level index, +5 and +8..+0x1c the 7-field spawn override (`engine+0x14a20..0x14a38`). Host applies it, loads the level, and announces it with 0x00. |
| 0x38 | 0xc | `FUN_1003c8ac`(entity, mask, set), Player natives | `FUN_10037880` | **Registry flag**: set (`FUN_1002f8fc`) or clear (`FUN_1002f8e0`) `mask` in the registry's `+0x468`; mask 0x10 also steps the counter `+0x478`. |

## How a join runs

1. Host: Host Multiplayer -> `MPBeginHost`; client: Join -> `MPBeginJoin` -> `JoinGame(n)`.
2. The client learns its index (**0x17**); the host sends **0x00** (game
   start: level, names). Both load the level.
3. Once the level is up (session tick, `engine+0x5c1`), the client sends
   **0x07** (ready) with its own **0x01 / 0x22 / 0x23**. When every peer
   is ready, the host sends **0x29** (its story state) and starts
   streaming every entity as **0x1d**, up to 50 a frame.
4. When the stream ends the host sends **0x07**, then **0x06** (your
   position: the host's own) and its own **0x01 / 0x22 / 0x23**, and
   switches to gameplay. The client places itself, runs the zone's `Init`,
   answers with 0x01 / 0x22 / 0x23 again and **0x30**.
5. From then on each side sends **0x01** at most every 0.75 s, **0x3c**
   every 10 s, and event tags as things happen.

## What this means for the port

- **Bandwidth is not a concern.** The busiest stream is 0x01 at 32 bytes
  every 0.75 s; the rest are events. A plain TCP connection is more than
  enough, with no prediction or rollback.
- **Reuse, not invention.** The entity stream (0x1d / 0x32) and the story
  state (0x29) are the per-class save chains and the level record the port
  already writes for `current.sav` (M121), so the serializers exist.
- **The avatar is a new kind of actor.** Model 0x5a, skinned by race and
  sex (0x22), held weapon (0x23), animations 5 / 8 / attacks / 6, a
  velocity it coasts on. It needs a HUD presence and to be a valid
  creature target (0x1e).
- **Two players is the shipped design.** Several messages only make sense
  with one other player (0x3d names no target; 0x01 rejects index >= 2).
  More than two would be a port extension, not a restoration.
- **Sender coverage.** Every sender is listed with its slot, but not every
  call site of every slot was traced (the loose search in M128 found
  callers for most, not all). Tracing the rest is part of wiring each one.

## What the port implements (M129)

The model above, over a socket: `net/mp_protocol` is this catalogue as code
(every tag at the size and offsets listed), `net/mp_transport` replaces
GAMECOMMS with a TCP link plus UDP LAN discovery, `net/mp_session` is the
session object's sequencing and timers, and `main.cpp` applies each event.

| | Tags |
|---|---|
| Sent and applied | 0x00, 0x01, 0x06, 0x07, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a, 0x1d, 0x1f, 0x21, 0x22, 0x23, 0x24, 0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x30, 0x33, 0x34, 0x35, 0x3c |
| Applied when received, never sent | 0x1b, 0x1e, 0x2c, 0x2d, 0x2f, 0x3b, 0x3d |
| Decoded and logged, not applied | 0x1c, 0x25, 0x26, 0x27, 0x31, 0x32, 0x37, 0x38, 0x39, 0x3a |

Where the port differs from the wire described above, and why:

- **0x17** is sent by the host as soon as a client connects; the client
  answers with **0x22**, which is how the host's lobby learns its name
  (Bluetooth supplied that on the device).
- **0x1d**'s save chain is the port's `SavedEntity` record -- the M121
  format -- and the stream ends with one extra record of type -1 whose id is
  the host's next free entity id.
- **0x29**'s level record is prefixed with its length.
- The client does not load the level on **0x00**: it answers 0x07 at once
  and loads on **0x06**, when the host's world has arrived. The packets and
  their order are the catalogue's.
- **0x2b** is also what tells the other player that an object left the
  world for any reason (a pickup taken, a bag emptied), in place of the
  per-case 0x1b / 0x31 senders.
- The level index is the port's own table (the zones in name order).

## Port status (M132)

Every tag is sent and applied by the port except: 0x39, whose receiver is a
no-op in the engine (it calls the area function with zero damage); and
0x1c and 0x26, which have receivers but whose session slots (`+0xe8`,
`+0x118`) are never called, so the engine never sends them. 0x26's mirror
also always passes 0 for the effect's "lit" argument. See
`docs/PORT_ROADMAP.md`, M130 and M132.
