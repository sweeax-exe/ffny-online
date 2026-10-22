# Rollback and run-time loads: the EAGL DynamicLoader, the async file layer, and what a restore leaves inconsistent

Read-only static study (nothing edited, built or run). Sources: `src/recomp/gen/recomp_00NN.c` (line numbers below are in
those files), the disassembly `tools/xboxrecomp/tools/disasm/output/asm/text.asm`, `game/default.xbe` strings (via
`scripts/string-va.py`), `src/hooks/rollback.c`, `logs/mcp/copy0.log.err`, and the notes `rollback-state.md`,
`rollback-step-structure.md`, `rollback-implementation.md`. Call-graph facts come from a script over the direct
`RECOMP_ABI_CALL`s only; **indirect calls (vtables) are not followed, so "no path" never proves absence.**

Tags: **[V]** read in code, data or a log; **[I]** inferred (reason given); **[U]** not settled (how to settle it is given).

## 0. Short answer

1. `sub_0010A7D0` is **`dlsym` of EA's "EAGL DynamicLoader"**, a tiny ELF-style run-time linker (strings `dlopen:`, `dlsym:`,
   `DynamicLoader::Resolve`, MIPS relocation names). It looks up a **symbol name** in the **ELF-hash table of one loaded object
   file** (a model/animation/render-method `.o`). `sub_0010AC20` is `DynamicLoader::Resolve`: it relocates the object in place and
   links its undefined symbols against the other loaded objects, a global symbol pool, a per-loader fallback callback, and
   `RUNTIME_ALLOC::<type>` pseudo-symbols. Everything (objects, bucket tables, symbol pool nodes) lives in the **game heap**.
2. **CModel's loader does no file I/O itself and is not asynchronous.** `sub_00026760` (= `CModel::_InitData`) receives an
   in-memory image, builds a DynamicLoader on it, resolves it under a critical section and returns. Files are read earlier, by the
   caller, through the file layer, which runs the read on a worker thread while the **calling thread blocks** until it is done.
   There is no "poll a flag later" step for model loads; the worker writes the buffer, a request object and queue links, all in the heap.
3. **Who triggers it:** statically proven only for match set-up (arena, venue, props, shadows, cinema; `sub_0008BE60 -> ... ->
   sub_00026A00`). In a fight the best candidate is **weapon creation** (`CWeapon::Make`, `sub_000BADB0 -> sub_00026A80 -> sub_00026760`).
   **No direct-call path from the render root exists**; a render-time creation would have to come through a vtable [U, one hook settles it].
4. **Inconsistencies after a restore** (section 3), strongest first: (a) the async file layer's request pool and its free-list head are split
   between the heap (restored) and `.data` 0x3C8F30..0x3C8FCC (deliberately not restored); (b) heap pages marked "left alone" because the
   worker thread wrote them (every file read counts as "another thread", `logs/mcp/copy0.log.err:11754`) while the allocator metadata
   around them is rewound; (c) host-side resources (D3D contiguous allocations made by the model instantiation, kernel objects) that a
   memory restore cannot rewind; (d) EAGL's `.data` shadow state (0x3A4000..0x3A7000, restored) against the D3D device (not restored).
5. **Hook points:** `sub_0010AC20_enter` for a dynamic load, `sub_001E5990_enter` / `sub_001E52C0_enter` for an async request issued /
   retired. Only `_enter` hooks exist, so "completed" is read from a flag or from the next hooked entry (section 4).

## 1. What `sub_0010A7D0` and `sub_0010AC20` are

### 1.1 The loader object ("obj", 0x430 bytes) [V]

Built by `sub_00109F80` (dlopen: parse + register, `recomp_0007.c:97495`). It takes the in-memory ELF image and **edits it in place**
(section header `sh_offset` fields become absolute addresses, `:97620-98170`; relocation-section type switch at `:97729`).

| Offset | Content | Evidence |
|---|---|---|
| +0 / +4 | next / prev in the global list, head at `MEM32(0x3A52A8)` | insert `:98355-98371`; unlink in `sub_0010AA90` `:99845-99862` |
| +8 | string table pointer (inside the image) | `:97825` |
| +0xC (byte) | "already resolved" flag; Resolve returns at once if set, sets it at entry | `:100181-100192` |
| +0x10 | symbol count | `:97796` |
| +0x14 / +0x18 / +0x1C | symbol table (16-byte Elf32_Sym), section header table (0x28 each), ELF header | `:97785,97608,97560` |
| **+0x20 .. +0x41F** | **256 buckets, filled with -1, ELF hash `& 0xFF`** | fill `:98202`; hash `sub_00109EB0` (`:97296`: `h=(h<<4)+c; h^=(h&0xF0000000)>>24; h&=~(h&0xF0000000)`) |
| +0x420 | chain array, `symcount * 4` bytes, tag "EAGL::dynamic symbols" (0x2B78F8) | alloc `:98206-98213` |
| +0x424 | per-symbol "defined" byte array, tag "EAGL::dynamic symbols 2" (0x2B7910) | `:98215-98222`, set `:98327` |
| +0x428 / +0x42C | owner handle / **unresolved-symbol fallback function** (CModel passes `sub_00025400`) | `:97548`, `:98196`; CModel args at `recomp_0001.c:1905` (0x25400 pushed) [I for the name of the callee] |

The table is **filled once, in `sub_00109F80`** (`chain[i] = bucket[h]; bucket[h] = i`, `:98308-98309`). Nothing inserts later.
The object, both arrays and every symbol-pool node are allocated through `MEM32(0x2E3988)`; the game sets that to `sub_00029800`
(`sub_000298F0`, `recomp_0001.c:10316`, free = `0x2E398C` = `sub_00029840`), which is the game's pool allocator `sub_00029500` followed by
a `memset(0)` (`sub_00029800`, `recomp_0001.c:10079`). So **the table and its entries are in the rolled-back heap** [V]; the only `.data`
pieces are the list head 0x3A52A8 and the pool objects below.

The loader *handle* (0x30 bytes, `sub_00029680` with a tag string) is a thin owner: `[0]=obj`, `[4]/[8]` = count / array of
(destructor, instance) pairs ("EAGL::dynamic destructor list", `sub_0010AB10` `:100029-100098`), `[0xC]` = list of runtime-alloc nodes
(`:100575-100578`), `[0x10]` image, `[0x14]` size, `[0x1C]` fallback, `[0x20..0x28]` patch-address vector (`sub_0010AA00`), `[0x2C]` "ready"
byte (set last, `:100973`) [V from the code, field meanings I].

### 1.2 `sub_0010A7D0` = dlsym(obj, name) [V]

`recomp_0007.c:99212-99400`. `h = sub_00109EB0(name)`; `i = obj.bucket[h]`; loop while `i != -1`: skip entries whose "defined" byte is 0, compare
`name` with `strtab + sym[i].st_name` byte by byte (the 2-bytes-per-iteration loop at `:99283-99326` is the string-compare loop of the crash),
on a match require `st_shndx > 0 && st_shndx < nsections` and return `section[st_shndx].addr + st_value`; else `i = chain[i]`.
`[ebp+idx*4+0x20]` in the crash is the bucket read; every pointer it follows (`obj+8`, `obj+0x14`, `obj+0x420`, `obj+0x424`, the strtab inside the image)
must be intact. **A crash there means one of those words is stale: the object, one of its two arrays, or the image it points into was
overwritten or freed**, or `edi` came from a list node that is no longer a loader object.

### 1.3 `sub_0010AC20` = `DynamicLoader::Resolve` [V]

`recomp_0007.c:100148-100983`. Called by `sub_0010B3E0` / `sub_0010B430` (the two constructors that wrap `sub_00109F80`, `:101038`, `:101085`)
and by `sub_00026760` (`recomp_0001.c:1976`). For every relocation entry whose symbol is undefined:

1. each object in the global list `0x3A52A8` except itself: `sub_0010A7D0(other, name)` (`:100295-100336`);
2. the fallback `[obj+0x42C]` (indirect call, `:100356`);
3. the **global symbol pool** at `.data` 0x3A52AC (`sub_00119470`, `:100403`). Entries are added by `sub_0010A9B0` and removed by `sub_0010A9D0`;
   callers at start-up (`sub_0010B650`: five `&EAGL::RenderMethodConstants::g...`), per venue (`sub_00018910`/`sub_00019370`:
   `venuespectexture%d`, `VenueEnvmapColour%d`, ...), `sub_0006D460` (`gShadowColour`), crowd init `sub_000DBBA0` (`InstanceTexture`),
   `sub_001826A0`/`sub_00182290` (`ColourModulator`), `sub_001197C0` (`EAGL::ViewPort::gp*Matrix`). Values are pointers to **heap or .data objects**;
4. a name starting with **`RUNTIME_ALLOC::`** (`strncmp(0x2B72BC, name, 15)`, `:100472-100484`): the rest is a type name, looked up in the registry at
   `.data` 0x3A52C0 (`sub_00117BB0` constructor, `sub_00117BD0` destructor); the constructor is called (`:100520`), then a 16-byte node
   `{destructor, ptr, ctx, next}` is allocated through `[0x2E3988]` and linked at `handle+0xC` (`:100547-100578`). Registered types:
   `EAGL::GeoPrimState` (`sub_00113F30`, allocates 0x54-byte objects) and `EAGL::TAR` (`sub_00102130`) (`sub_0010B650` `:101600-101608`).

The resolved symbol is then **rewritten in the requesting object's symbol table** (`st_shndx=1`, `st_value = addr - section1.addr`, `st_other` = 2 other object /
3 callback / 4 pool / 5 runtime alloc, `:100373-100390`, `:100415-100422`, `:100587-100590`), the relocation switch applies types 2/4/5/6/9 (`:100645-100668`)
and finally `sub_0010AB10` (`:99932`) walks the object's sections, finds a **type handler** in the registry at `.data` 0x3A52E8
(`sub_00117AF0`; types `EAGL::TAR`, `RM`, `Model`, `VB`, `IndexPushBuffer`, registered by `sub_0010B650` `:101560-101596`) and calls it to build the run-time
object. The `VB`/`IndexPushBuffer` handlers (`sub_00114ED0`, `sub_00114F10` `:19690-19749`) copy the data into **contiguous (GPU-visible) memory**
via `sub_0021F140` -> `sub_001F9DC6` (`MmAllocateContiguousMemoryEx` wrapper) and set the dirty byte `0x3A61F8`; their destructor entries zero
`0x3A6E20[i]` and call `sub_0021BB10`. **Model instantiation therefore creates D3D resources, not only heap objects** [V up to the handler bodies; the
contiguous-allocation claim is from `sub_0021F140`'s tail-jump, I for what `sub_001F9DC6` does exactly, consistent with `d3d-pushbuffer-wrap.md`].

## 2. How CModel's dynamic loader loads, and who calls it

### 2.1 Inside `sub_00026760` (`CModel::_InitData`, `recomp_0001.c:1854`) [V]

1. `sub_00029680(0x30, "CModel::_InitData m_pDynamicLoader ex", tag)` allocates the handle (`:1891`; `sub_00029680` = `sub_00029500` + `memset`, `:9713`).
2. `sub_0010B430(handle, image, size, ..., 1, 0x25400)` (`:1912`) = `sub_00109F80` (parse, build hash table, **insert in the global list**) and, if that
   succeeded, `sub_0010AC20`.
3. `sub_0010A970(handle, "Model", &out)` (`:1930`; string 0x2B7230) = lookup of the exported symbol `Model`, the model data root, stored at `this+0x12C`.
4. `MEM32(0x33DB50) = this` ("model being loaded"), **`sub_001ECF00(0x2FB000)` (enter critical section)**, `sub_0010AC20(handle)`, `sub_001ECF10(0x2FB000)` (leave),
   `MEM32(0x33DB50) = 0` (`:1968-1983`). The return address of the lock call is `0x26837`, of Resolve `0x26845`: a stack scan that reports 0x26837 saw a dead slot
   left by the lock call, and the crash was inside Resolve while holding that lock [I].
5. Texture, light-block and bone set-up (`sub_00022A90`, `sub_00027470/27620/27740/27240`) follow; the object is registered with the render subsystem
   elsewhere (`sub_0001BD80` on registry slot 6, seen in `CWeapon::Make`, `recomp_0005.c:50357`).

No file call is made in `sub_00026760`. **Its image comes from the caller:**

* `sub_00026940` (`:2112`): builds a path (`sub_00200C41`, "%s.o"-style), loads the whole file with **`sub_001E46E0`** (`:2187`), takes its size via
  `sub_00029870`, calls `sub_00026760`.
* `sub_00026A00` / `sub_00026A80` (`:2247`, `:2332`): the image is **already in memory** (a block the caller read earlier, or copied from resident data).
* `sub_00018ED0` (`recomp_0000.c:24548`, "CRenderSubsystem::NewDynamicLoader") reads a file with `sub_001E46E0` and calls `sub_0010B3E0`; same shape in
  `sub_00021710` (CAnimationBank), `sub_000231C0` (CMorph), `sub_000217B0`, `sub_000DBBA0` (crowd), `sub_000CB3B0`/`CB9A0`.

### 2.2 Which caller is on the simulation side, which on the render side

* **Match set-up, inside the screen update (`sub_0008BE60`), by direct calls [V]:**
  `8BE60 -> 8BB00 -> 8B730 -> A6AF0 -> 977B0 -> 9F030` (string `assets/misc.viv|%s.o`) `-> 26A00 -> 26760`;
  `8B730 -> 64170 -> 9AB90 -> 980B0 / 98F60` (`assets/misc.viv|V2SD_%d.o` shadow models) `-> 26A00`;
  `8B730 -> 87090 -> A0C40 -> 217B0 -> 10B430 -> 10AC20`. `sub_0008B730` also loads `motion/main_all.dat`. These are the loading states of the
  state machine, not the live-fight states 4/5/8.
* **In a fight [I]:** `sub_000BADB0` (`CWeapon::Make`, strings `CWeapon::Make::m_pModelData/m_pModel`) copies resident model data into a new heap block,
  calls `sub_00026A80` (`recomp_0005.c:50299`), then adds the model to the scene and builds the shadow (`sub_000BABD0`). Its only direct caller is `sub_000BAF90`,
  whose only caller is `sub_000BCC50` (no direct callers; the weapon-manager spawn path that also calls `sub_00044AC0`, `rollback-step-structure.md:214`).
  So weapon creation is a **sim-side path that runs dlopen + Resolve + instantiation (heap + contiguous memory)** whenever a weapon appears.
* Other users: cinema `_LoadShot` (`sub_00071B60/72BC0/73040`, `sub_00073FD0`), crowd (`sub_00083A50` ...), shop/venue/trophy screens: not fight code.
* **Render side:** the direct-call closure of `sub_000796A0` + `sub_00077740` (331 functions) contains none of `sub_00026760`, `sub_0010AC20`,
  `sub_001E5990`, `sub_001E6060`, `sub_001E46E0`. Its only allocator caller is `sub_00045100` (`CLocalization` text buffer, via `sub_000B5AD0 -> B2350 -> B2190 -> 45160`),
  so **render does allocate from the game heap each frame** (HUD text) [V]. Whether a draw callback can create a CModel through a vtable is [U]: log the
  return-address chain in `sub_0010AC20_enter` (section 4) and compare with `N.resim` / the update counter.

### 2.3 The file layer: synchronous for the caller, a worker does the I/O [V]

* Init: `sub_0001A920` (`recomp_0000.c:28574`) installs alloc/free callbacks `sub_00029500` / `sub_000294D0` in `0x2F820C` / `0x2F8210` (`sub_001E5860`) and calls
  `sub_001E5A60(0x80, 0x100, 0x80)` -> `sub_001E4EC0` (`recomp_0012.c:187988`, disasm `text.asm:815066`). It allocates **one ~9 KB heap block**
  (`0x80*20 + 0x80*0x30 + 0x170` = 0x2370 bytes, align 0x100, tag 0x2D0310) holding a handle table (`sub_001E8830`), **two worker slots of 0xB8 bytes**
  (`MEM32(0x3C8F30)` = block + 0xA00 [I from the lea arithmetic]) and **128 request objects of 0x30 bytes** pushed onto the free queue
  `.data` 0x3C8F34. Second queue object 0x3C8F94, lock 0x3C8F6C, flags 0x3C8F8C/F90/FCC are all in the skipped page 0x3C8000..0x3CA000.
* A request: `sub_001E5990(op, slot, arg)` (`recomp_0013.c:575`) pops a request from the free queue (`sub_001EE300`), fills `[+8]=op`, `[+0x11]=slot`, `[+0x1C]=arg`,
  `[+0x20]=0` (callback), pushes it on the slot's pending queue (`slot+0x14`) and creates the worker **lazily** (`sub_001E5880`, `recomp_0013.c:442`: queue init with key
  function `0x1E4970` (= `((req[+4]>>5)&0xFFFFFF) | req[+0x11]<<24`, which is why `0x1E4970` shows up as a "start routine"), critical section `slot+0x8C`, semaphore
  `slot+0x84`, `NtCreateEvent` handle at `slot+0xAC`, then **`sub_001ED4B0` creates the thread with entry `sub_001E4B40`** and argument = slot index). Callers of
  `sub_001E5990` are the 11 builders `sub_001E5AC0, 5B60, 5C00 (open), 5CA0 (close), 5CF0, 5D60, 5DD0 (size), 5E70, 5ED0, 5F30 (read), 5FF0 (seek)`: **one funnel for every request** [V].
* The worker (`sub_001E4B40`, `recomp_0012.c:187353`): picks the request, switches on `req[+8]` (0 open via `sub_001E4A40`, 2/3 read/write via `sub_001E8BA0/8DD0`
  = `NtReadFile`/`NtWriteFile` thunk at `0x1F8D0E`, 9 refpack file with 0x10-byte header), then **writes**: the user buffer (`req[+0x2C]`, by the kernel), `req[+0x10]`
  state byte (1 done, 0xFE error), `req[+0x14]` result code, `req[+0x18]` OS handle, `req[+0x24/0x28]`, moves the request to the slot's done queue (`slot+0x4C`,
  `sub_001EE270`, `:187882-187885`), calls the **completion callback `req[+0x20]` on the worker thread** if set (`:187938-187944`; the 11 builders never set it, so
  model/asset loads have none), signals the event `slot+0xAC` (`:187954`).
* Completion for callers: `sub_001E3B30/3BF0/3C30/3C70/3CB0/3D10/3D50/3D90` are the public wrappers; each does *create request, `sub_001E5420` (wait), `sub_001E4FC0`
  (status), `sub_001E52C0` (take result and retire the request)*. `sub_001E5420` (`recomp_0012.c:189155`) blocks: on the main thread it loops
  `sub_001ECE20(0)` (runs the host-timer callback list at `.data` 0x3C9508.., `recomp_0013.c:24843`) + `Sleep(1)`; on other threads it waits on the slot event (`sub_001F6057`).
  `sub_001E52C0` (`:188856`) frees the filename copy (`req[+0x2C]` via `0x2F8210`), zeroes `req[+4]` and returns the request to the free queue.
* `sub_001E6060(slot, cb, a, b)` (`recomp_0013.c:1791`) does *not* hand work to a thread: it runs `cb` on the **calling** thread under the slot's critical section and
  sets `slot[+0xB4]` = which worker the requests created inside should use; `sub_001E4100 -> 1E6060(0x1E3FF0)` is "load whole file" and `sub_001E46E0` adds the
  copy/decompress (`sub_001E9000`, refpack) into a second block from `0x2F820C`.
* **Observed in a fight** (`logs/mcp/copy0.log.err:11713-11723, 11754-11761`): 11 reads of 1152-5771 bytes on handle `0x4800001C`, data starting `10 FB` (RefPack),
  destinations `80DEE600, 80DEEF00, ... 80DF2500` (stride 0x900), all at the 60th update of the fight, **all classified "another thread"** by `track_read`
  (`rollback.c:245-262` compares the *host* thread id; the worker is the one calling `NtReadFile`, while the game thread is blocked in `sub_001E5420`). Nothing else
  non-music was read in the ~2,000 recorded steps.

## 3. State that is inconsistent after restoring an older heap + `.data`

Restored by `rollback.c` (`:42-64`): `.data` 0x2D54E0..0x3C92C8 minus 0x2E6000..0x2E8000, 0x3A9000..0x3B1000, 0x3C8000..0x3CA000, the sound globals; heap 0x80003000..0x82A0D000
page by page minus pages in `s_skip`. **Not restored:** the D3D/XGRPH/DSOUND sections (0x210E00..0x27CD84, includes the D3D device struct 0x234770), `.rdata`, `.data` 0x3C8000..0x3CA000
and everything above 0x3C92C8 (host-timer list 0x3C9508.., the loader/timer words), contiguous memory outside the heap block, host kernel objects.

### 3.1 EAGL state in `.data` 0x3A4000..0x3A7000 and the heap [V addresses, I for roles]

`0x3A5220/0x3A5260` (render-method objects, `recomp_0008.c:7047...`), `0x3A52A0` (counter), `0x3A52A5/6` (once-flags), **`0x3A52A8` loader list**, **`0x3A52AC` global symbol
pool**, **`0x3A52C0/D4` runtime-alloc registry**, **`0x3A52E8/FC` type-handler registry**, `0x3A5388` (EAGL instance), `0x3A538C-94`, `0x3A56A4`
(result of `sub_0021D310`), **`0x3A5700` = a static C++ object (`sub_002104B0` calls its constructor `sub_00113F00`, destructor `sub_00210C50`) used by the render-method / GeoPrimState
code `sub_0010EF00`, `sub_0010F1E0`, `sub_0010F950`** (setter methods `sub_00113500..136F0`) - a configuration/state-building context, not the device [I], `0x3A5758-0x3A5770`, `0x3A61F8`, `0x3A6E20[]`.
All of it is inside the restored range, so against the heap it is consistent. It is inconsistent against anything that is not restored:

### 3.2 The list, strongest first

1. **Async file layer split across restored and non-restored memory [V structure, I that it bites].** Queue head/tail/count/lock of the request free list are in `.data` 0x3C8F34
   (skipped); the request objects, their `next` links, the two slots (queues at `slot+0x14/0x4C`, "started" word `slot[0]`, event handle `slot+0xAC`, semaphore, critical
   section image) are in the heap block above (restored whenever the game thread wrote its page first; "left alone" only if the worker or the mixer wrote it first).
   `sub_001E5990` runs on the game thread and writes request fields, so the page is very likely "known" and is **rewound**: the live free-list head then points to
   nodes whose `next` words are old (list corrupt: lost or duplicated requests, hang in `sub_001E5420`, crash in `sub_001EE300`), and a slot whose worker was
   created after the save reads `slot[0]=0` and is **created a second time** on the same queues. Settle it: dump `MEM32(0x3C8F30)` in a fight and look it up in `s_skip` /
   `s_known`; or make the whole 0x2370-byte block "left alone" (as `FRAME_QUEUE` is, `rollback.c:358`), which is correct because this state is transient and must not be rewound.
2. **File reads are attributed to "another thread" [V].** `track_read` (`rollback.c:245`) and the access-violation path use `GetCurrentThreadId() == s_game_tid`. The
   file worker performs the `NtReadFile` while the game thread waits, so a read into a still-guarded page marks that page **foreign: never saved or restored again**
   (`first_write`, `rollback.c:200-216`). Pages that hold file data are harmless; but a file buffer is allocated from, and later freed back to, the same heap: the page is
   later reused by game structures (loader objects, bucket/chain arrays, image copies, particle/animation blocks) that now **never rewind** while the allocator metadata and
   the `.data` roots (0x3A52A8 list head, pool objects) do. A loader object on such a page is exactly the stale-word crash of section 1.2. In the log the 11 reads at
   `copy0.log.err:11754` produced no new "another thread writes" line, so those particular pages were already unguarded; the mechanism is still open for the first read into a
   fresh page [U: grep `rollback_save` output for "another thread writes" over a run with weapon spawns].
3. **Host-side resources created by instantiation [V creation, I consequences].** Model instantiation allocates contiguous memory (VB, IndexPushBuffer, textures) through
   the host bump allocator (never reused without `RECOMP_HEAP_RECLAIM`, `rollback-state.md` section 1.3). A load back to before a weapon spawn **leaks** that memory and the
   re-run allocates again (bounded by the 21.9 MB outside the heap block; count weapon spawns x rollbacks). A load back to before a *destruction* resurrects objects whose
   contiguous blocks were freed on the host; harmless while freed blocks stay mapped, wrong if freeing ever decommits. The kernel objects behind the worker slots (event,
   semaphore, critical section) are host shadows keyed by guest address; their guest images are rewound with the page, the shadow state is not.
4. **EAGL shadow state against the D3D device [I].** The render library's cached state (`0x3A5xxx/0x3A6xxx`, dirty byte `0x3A61F8`, per-stage table `0x3A6E20`) is restored
   to the save while the device (0x234770, push buffer, GPU) has moved on through every render since. Expect state-cache misses (skipped state changes, wrong textures
   for a frame), not crashes, unless a cached pointer names a contiguous block freed meanwhile (item 3).
5. **Render allocations between save and load [V that they exist].** HUD text allocates every frame (`sub_00045100`). Because the game thread writes them, those pages are
   in the change set and the allocator is restored consistently; renders are not re-run, so nothing is double-allocated. No inconsistency expected from this alone.
6. **Global lists point at objects created in render or sim after the save [I].** Consistent as long as every page holding an object is restored (item 2 aside): head 0x3A52A8
   and `obj+4` back-links (written into the *older* object's page, marking it changed) return together.

## 4. Hook points

Only `void sub_XXXXXXXX_enter(void)` hooks exist (the generated function calls them first, from every caller; `CLAUDE.md`, `src/recomp_manual.c:41-84`; `g_ecx`, `g_esp` give
registers and stack at entry). There is no exit hook and a `_gen` wrapper is bypassed by direct callers.

| Need | Hook | Arguments at entry | Why this one |
|---|---|---|---|
| (a) a dynamic model/bundle load starts | **`sub_0010AC20_enter`** | `g_ecx` = handle (`[0x10]` image, `[0x14]` size); `MEM32(g_esp)` = return address (`0x26845` CModel, `0x10B423` `sub_0010B3E0`, `0x10B47E` `sub_0010B430`) | all three constructors reach it by direct calls; every dlopen user (CModel, CAnimationBank, CMorph, render methods, crowd, cinema) ends here; synchronous |
| (a') earlier, when the object enters the global list | `sub_00109F80_enter` | `g_ecx` = handle | only callers are the two constructors; list insert happens inside |
| (b) a dynamic load completed | none directly. Read **`MEM8(handle+0x2C) == 1`** (set at `recomp_0007.c:100973`, after instantiation) or treat it as complete at the next `sub_0002C530_enter` (the update start) because Resolve cannot return half done on the game thread. `sub_0010AB10_enter` marks "relocation done, instantiation starts" | `g_ecx` = handle | Resolve is not re-entrant on one thread (`obj[+0xC]`, lock 0x2FB000 around the CModel path) |
| (a) an async file request is issued | **`sub_001E5990_enter`** | `MEM32(g_esp+4)` = op (0 open, 2 read, 3 write, 9 refpack file ...), `+8` = slot (0/1), `+0xC` = arg | single funnel for all 11 request builders |
| (b) an async request is finished and retired | **`sub_001E52C0_enter`** | `MEM32(g_esp+4)` = request handle | every public wrapper calls it after the wait; the request is already done. `sub_001E5420_enter` = "caller starts waiting" (same arg); `sub_001E4FC0` = non-blocking status poll |
| (b') worker finished a request | nothing hookable (mid-function in `sub_001E4B40`); watch `req[+0x10]` or use `RECOMP_WATCH_WRITE` | | |

Suggested uses (not done):

* **Attribute reads correctly:** in `sub_001E5990_enter` / `sub_001E52C0_enter`, when `GetCurrentThreadId() == s_game_tid`, `++/--` a counter; in `track_read`, treat a read
  from a non-game thread as the game's while the counter is above zero (the game thread is provably blocked waiting for it).
* **Quiet check at the save point:** at `sub_0002C530_enter`, assert `issued == retired` for the game thread (no request in flight) and record `last_dynload_update` from
  `sub_0010AC20_enter`. A load whose target save is older than `last_dynload_update` crosses a host-resource creation (section 3.2 item 3); count or forbid it.
* **Find out what actually loads in a fight:** log `{update, resim?, thread, MEM32(g_esp), handle, image size}` from `sub_0010AC20_enter` for a 5-match run. That settles section 2.2
  (weapon spawn vs a render-time creation) with a single line per load.

## 5. Open points and how to settle them

* [U] Whether `sub_000BCC50` (weapon spawn) runs inside `sub_0008BE60` during a live fight: `sub_0010AC20_enter` log above, or `RECOMP_WATCH_EXEC` on `0xBADB0`.
* [U] Whether any model is created from a render callback: same log, filtered on "update counter did not advance since the last `sub_00087700_enter`".
* [U] Address of the async block: read `MEM32(0x3C8F30)` in a fight; expected block = that value - 0xA00, size 0x2370 [I].
* [U] Which of the two worker slots is created when: log `sub_001E5880` entries (`ebx` = slot) once; if both exist before the first save, the double-creation case of 3.2 item 1 cannot happen.
* [I] `0x3A5700` role: confirm by reading `sub_00113E80` (constructor) and the string it parses (`UID`, `XBOXEXTOBJ` at 0x2B8480/0x2B8484 are used by `sub_00113F30`).
