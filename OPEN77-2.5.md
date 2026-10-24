# ArchiveXL for Cyberpunk 2.5 (Open77 fork)

**Unofficial.** Branch `open77/cyberpunk-2.5` of [psiberx/cp2077-archive-xl](https://github.com/psiberx/cp2077-archive-xl),
from `d56db6e` (1.27.5), ported by the Open77 team to Cyberpunk 2077 2.5 (FILEVER 3.0.88.46235) while waiting for
the official update. It is not supported by psiberx: report problems with this build here, not upstream. When the
official 2.5 release ships, use it instead.

Releases are tagged `v1.27.5-open77.N`. The package is game-root relative, like the official one: extract it into
the game folder.

## Building

As upstream (`xmake build -y`), with two differences:

- `vendor/RED4ext.SDK` is pinned to `0f5eba42` on [wopss/RED4ext.SDK](https://github.com/wopss/RED4ext.SDK)
  ("Add support for patch 2.5").
- `vendor-patches/RED4ext.SDK/include/` holds the 33 generated enum headers whose members 2.5 renumbered
  (`EquipmentArea`, `ItemType`, `StatType`, `MappinVariant`...), regenerated from the 2.5 RTTI. `xmake.lua` searches it
  before the SDK.

## What works, what is off

| Feature | 2.5 |
|---|---|
| Archive extensions (`.xl` next to mod archives) | works (open77.5) |
| Streaming blocks, sector node deletions | verified in game: a world with 127 added meshes, lights and collision plus a deleted cooked mesh |
| Sector node moves, cooked collision actors, instanced meshes, decals | ported (open77.2), not checked in game yet |
| Resource patches, links, copies, factories, localization, journal | load without errors, not checked in game yet |
| Dynamic appearances, attachments, garments | load without errors, not checked in game yet |
| Widget spawner (`item:controller` names) | off: `InkSpawner_FinishAsyncSpawn` not located; Codeware 1.14+ does it anyway |
| WorldWidgetComponent limit patch | off: the function was rewritten on 2.5 |

A feature that is off logs a warning at startup and leaves the others running. Reports of what works or breaks
in game are welcome.

## Addresses (open77.1)

`src/Red/Addresses/Library.hpp`: the address-library hashes 2.5 changed, matched function by function between the
2.31 and 2.5 executables (`AppearanceChanger_GetSuffixValue` takes the suffix record by reference now,
`TweakDB_Load` takes a CString, `ResourceLoader_OnUpdate` receives the loader at +0x60...).

open77.3, through their 2.31 callers translated to 2.5: `MappinSystem_GetPoiData` = 2918455690 (both 2.31 callers
call it; the same (system, hash) lookup of 0x20-byte entries at `[system + 0x58] + 0x40`: the Journal extension
loads again) and `InkWidgetLibrary_AsyncSpawnFromLocal` = 2342073570 (called by `AsyncSpawnFromExternal`; both take
a bool more on 2.5, passed through by the hooks).

Not located yet (hash 0): `InkSpawner_FinishAsyncSpawn` (slot 2 of a vtable whose class and installers changed) and
`InkWorldLayer_UpdateComponents` (rewritten). A zero hash makes its hook fail; each extension loads in its own try.

open77.4: the InkSpawner hooks are installed at engine init, outside that try: with the zero `FinishAsyncSpawn`
hash, `OrThrow` was an unhandled `std::runtime_error` (0xE06D7363) that closed the game about 10 s after launch when
Codeware is absent. `InkSpawnerExtension::Load` now skips the extension (logged warning) while the hash is 0.

open77.5: 2.5 renumbered the depot's archive scopes (the SDK still says `Mod = 4`). Both 2.5 functions that build the
mod group from `mods` + `archives` look it up with 6 (rva 0x1259754, 0x12594ac; 2.31 used 4 at 0x1fbc54a); 4 is the
patch group now and 5 a new group loaded at init. With 4, ArchiveXL found no mod group, read no `.xl` next to the
mod archives and patched nothing. `Red::ModArchiveScope` (= 6) replaces `ArchiveScope::Mod` in the extension loader
and `ArchiveService`. The `ArchiveGroup` layout (0x38) is unchanged.

open77.6: the character customization menu could stop on the SDK assertion `aIndex < m_size` (DynArray.hpp:122).
`CustomizationExtension::FixCustomizationComponents` read `partsOverrides[0]` of every definition of an
appearance resource in the `player_customization.app` scope, without checking the array; a definition with no parts
overrides now leaves its components as they are. The customization classes' RTTI layouts are unchanged on 2.5, and
so are the offsets read in the system (+0x48 / +0x58 resources) and `CMesh` (+0x218 material lock).

The streaming system's world handle stays at system + 0x270 (`H335484407` writes it there on both builds): the
`.devices` / `.psrep` patches are unchanged.

## World streaming (open77.2)

2.5 sectors (version 68) changed the structures `WorldStreamingExtension::PatchSector` works on, measured on the 2.5
executable:

- `worldCompiledNodeInstanceSetupInfo` (0x90): the node index is the low 19 bits of the word at 0x70 (the node
  buffer post-load `H1135087376` reads `dword [setup + 0x70] & 0x7FFFF`; 2.31 read `[setup + 0x78] & 0xFFFFF`),
  then the two distances at 0x78/0x7C, the instance index at 0x80 (the entry of a cooked node; 0xFFFFFFFF
  otherwise) and the data group at 0x84.
- The setup buffer grew to 0x30: the sector's node definitions are at node buffer + 0x30 (was 0x28), its node
  references at + 0x40.
- 2.5 cooks the world: the static meshes, decals, roads, cables, proxies and collision of a sector are each one
  `worldCooked*` node holding an entry per placement. A placement's edits stay on its setup: a deleted cooked
  placement gets a zero scale and keeps its node (the node holds the other placements' entries).
- `worldCookedCollisionNode` (0x80, vtable `0x35d5fa0`): its entries are a byte array at 0x40 (0x30 each; the stride
  is the dword at 0x50, read by the node's entry-bounds virtual `0x20b8d80`), its compiled data at 0x58. An entry
  holds the nine counts of the 2.31 `worldCollisionNode` and the offset of its run of the compiled data, whose
  layout is the 2.31 one (actors 0x30, shape infos 0x20, positions, rotations, scales, materials, presets...).
  Actor deletions and moves work in the run. A shape is switched off with a "No Collision" preset only when the
  run already lists one (its presets cannot grow); otherwise the shape stays and a warning is logged.
- The uncooked `worldCollisionNode` shrank (0x120 -> 0xC0): its actors are not addressed any more (the game ships
  no uncooked collision on 2.5).
- `worldInstancedMeshNode` (0x98, vtable `0x305c058`, serializer `0x6cd098`): world transforms at 0x38, entries at
  0x70 (0x40 each: bounds, mesh, appearance...), ranges (start, count, u16) at 0x80, one per entry: a placement's
  instances are its range, edited and deleted (zero scale) one by one; mesh and appearance mutations go to its entry.
- `worldInstancedDestructibleMeshNode`: the cooked shared transforms are gone; its instances are the node's own
  array at 0xB8 (relative to the placement).

The `.xl` format is unchanged: `index` is the setup (placement) index, `type` the node's real type
(`worldCookedStaticMeshNode`, `worldCookedCollisionNode`...), `expectedInstances` / `expectedActors` count the
placement's own instances or actors.

## Licence

MIT, as upstream (`LICENSE`). ArchiveXL is made by Pavel Siberx (psiberx); this fork only adapts it to 2.5.
