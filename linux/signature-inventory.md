# PalSchema signature inventory for PalServer-Linux-Shipping v1.0.5.102999

Sources: PalSchema include/SDK/PalSignatures.h (24 entries) plus FindComponentTemplateByName,
which the code asks for but the map never had. Matched against:

- the export table (notes/PalServer-dynsym-v1.0.5.102999.txt, 30,470 vtables, no UE function bodies)
- the native UFunction dump from shadow run 13 (notes/natives-v1.0.5.102999.txt, 19,692 natives,
  9,651 in /Script/Pal), produced by nativedump/main.cpp in about one second after on_unreal_init
- UTF-8 and UTF-16 strings in the binary
- what libUE4SS.so already exports on this port

Return-address handling is not a Linux problem: the hand-rolled detour enters via jmp, so
__builtin_return_address(0) in the detour is the original caller, same as _ReturnAddress on Windows.

## Columns

kind: export | vtable | ue4ss (libUE4SS already provides it) | string (log or FName literal to xref)
| anchor (call it from an exported vtable slot or a reflected native and follow) | ghidra (nothing to
hold on to, manual RE) | drop (not needed on Linux)
reflected: is the target itself a native UFunction in the dump (hookable with zero RE)
feature: which PalSchema loader needs it
ret-addr: the hook compares the caller address against this site

| # | target | kind | reflected | feature | ret-addr | notes |
|---|--------|------|-----------|---------|----------|-------|
| 1 | FName::Constructor | ue4ss | no | core | | FName(const char16_t*, EFindName) exported by libUE4SS; proven in run 11/12 |
| 2 | FName::ToString_Wchar | ue4ss (game thread only) | no | core | | port routes ToString through KismetStringLibrary::Conv_NameToString via ProcessEvent; game thread only, slow; the port's own dlsym for the real one failed. PalSchema names tables inside the Serialize hook on the loading thread, so this is NOT a drop-in there; see row 26 |
| 3 | FMemory::Free | export | no | core | | operator delete(void*) exported at 0x6f687e0 is a bare jmp to FMemory::Free at 0x78118b0; GMalloc lives at 0xc07f6a8 (Free is vtable slot 7). libUE4SS also exports FMalloc::Free |
| 4 | UObjectGlobals::StaticFindObject | ue4ss | no | core | | StaticFindObject_InternalNoToStringFromStrings, proven run 12 |
| 5 | GetObjectsOfClass | ue4ss | no | core | | UObjectGlobals::FindObjects / ForEachUObject, proven run 13 over 44,095 objects |
| 6 | FField::IsA | ue4ss | no | core | | exported by libUE4SS |
| 7 | FFieldClass::GetNameToFieldClassMap | ue4ss | no | core | | exported by libUE4SS; confirm it is not a throwing stub on Linux before relying on it |
| 8 | AsyncTask | drop | no | core | | replace with the ProcessEvent pre-callback game-thread dispatch used in runs 12 and 13 |
| 9 | AGameModeBase::InitGameState | vtable | no | unused | | port hardcodes slot 0x740 for PalServer; PalSchema's source never calls GetSignature for it |
| 10 | UBlueprintGeneratedClass::PostLoadDefaultObject | vtable | no | unused | | UBlueprintGeneratedClass vtable exported at 0x204f050, slot not yet identified; PalSchema never calls it |
| 11 | UDataTable::Serialize | vtable | no | raw tables, items | | slot 27, proven run 12 |
| 12 | FPakPlatformFile::GetPakFolders | LOCATED 0x9dfb450 (Ghidra, run 79) | no | raw tables (optional extra pak dir) | | UTF-16 "%sPaks/" at 0x7a57f2 has exactly one referencing function in .text, 0x9dfb450, which formats it three times (the three pak roots). That is GetPakFolders. Codex flagged this; earlier draft wrongly said no string |
| 13 | UClass::GetDefaultObject(bCreate) | anchor | no | BP/Pal mods | | UE4SS GetClassDefaultObject only reads the field; PalMonsterModLoader needs the creating variant. UClass vtable exported at 0x1a50df8 |
| 14 | UClass::AssembleReferenceTokenStream | LOCATED 0x7ad40f0 (Ghidra, run 79) | no | BP/Pal mods | | "AssembleReferenceTokenStream for %s called on a non-game thread" is in the binary; one xref |
| 15 | UStruct::StaticLink | LOCATED 0x7a39e60 (Ghidra, run 79) | no | BP/Pal mods | | UStruct vtable exported at 0x1a506d8; Link and Bind call StaticLink |
| 16 | UBlueprintGeneratedClass::FindComponentTemplateByName | ghidra | no | BP/Pal mods (component overrides) | | absent from PalSchema's own map too; no string, not reflected |
| 17 | UWorld::CleanupWorld | LOCATED 0xab1e020, hook proven run 79 | no | spawn mods | | UEngine 0x1ed5e08 / UGameEngine 0x20ef378 / UWorld 0x2259610 vtables exported; LoadMap and world teardown call it |
| 18 | UPalItemSlot::UpdateItem_ServerInternal | ghidra | no | item save safety | | PalItemSlot has 19 natives (GetItemId, GetStackCount, OnRep_ItemId...), this is not one; no string |
| 19 | UPalItemContainer::ApplySaveData | ghidra | no | item save safety | yes | return site after the UpdateItem call; PalItemContainer has 12 natives, none of them this |
| 20 | UPalDynamicItemWorldSubsystem::Create_ServerInternal | ghidra | no | item save safety | | class has no natives at all |
| 21 | UPalDynamicItemWorldSubsystem::ApplyWorldSaveData | ghidra | no | item save safety | yes | "ApplyWorldSaveData.AfterApply" strings exist but belong to the map object manager |
| 22 | ValidateWorldSaveDynamicItemStaticIds | ghidra | no | item save safety | | free function, no string |
| 23 | ValidateDynamicItemSaveData | ghidra | no | item save safety | | free function, no string |
| 24 | FPalPlayerRecordDataRepInfoArrayThreadSafe_IntVal::ApplyDataMap | ghidra | no | item save safety | | struct's TCppStructOps vtable is exported (type anchor only) |
| 25 | CraftItemCount_ApplyDataMapReturn | string | no | item save safety | yes | FName literal "CraftItemCount" is in the binary; its xref is the call site once 24 is known |
| 26 | FNamePool / real FName::ToString | string (via dump thunk) | no | core, names off the game thread | | SOLVED statically 2026-09-19: KismetStringLibrary::Conv_NameToString thunk 0xa511690 calls FString FName::ToString() const at 0x7977420 (sret rdi, this rsi). Its Number==0 path reads the pool inline: init flag 0xc0b2478, FNamePool 0xc0b2480, block table 0xc0b24c0 (block = *(0xc0b24c0 + (idx>>16)*8), entry = block + (idx&0xffff)*2, header u16: bit0 wide, len = header>>6, chars follow). Numbered names go through 0x79774c0. Run 14 (20:19): probe v0.6 called it inside the Serialize hook on the async loading thread for four tables; every name matched the deferred game-thread result. PROVEN |
| 27 | UStruct::InitializeStruct / DestroyStruct (virtual) | vtable | no | row insertion, row removal, PalSchema AddRow | | PalServer slots 0x300 / 0x308 (UE4SS 5.1 map says 0x2F8 / 0x300, port does not shift UStruct's map). Proven run 20 by insert, engine lookup, remove. UE4SS's UDataTable::AddRow/RemoveRow wrappers must not be used on this port until its UStruct map is fixed |
| 28 | UE4SS VTableLayoutMap corrections: every entry after the destructor is +8 versus UE4SS's MSVC-derived 5.1 maps, in every class | vtable | n/a | every UE4SS virtual wrapper PalSchema uses | | Runs 22 and 33. palslice/shim/vtable_baseline_5_01.hpp + fix_vtable_maps() rebuild FProperty, FField, FNumericProperty, UObject, UStruct, UClass, UDataTable, UField, ICppStructOps as baseline+8 at mod start. Proof: GetCPPType at 0x70 builds "float", 0x68 is PassCPPArgsByRef; UDataTable FinishDestroy/Serialize at 0xC8/0xD0/0xD8; InitializeStruct 0x300; FMalloc Free 0x38 |
| 29 | UE4SS FMemory on the port | detour | n/a | anything that hands engine memory to a UE4SS container (FString from native ToString, RowMap growth, AddRow rows) | | The port's FMemory::Malloc/Realloc/Free are libc calls and its GMalloc heuristic is wrong (0xbd8b870 vs real 0xc07f6a8). palslice install_allocator_hooks() detours the three exported functions to FMallocBinned2 with ownership-routed frees. Proven run 33. Heuristic; upstream fix is finding the real GMalloc |

## Counts

| bucket | rows |
|--------|------|
| proven on the shadow (rows 1, 3, 4, 5, 11, 26, 27, 28, 29) | 9 |
| provided or replaceable, integration unverified (rows 2, 6, 7, 8) | 4 |
| unused by PalSchema (rows 9, 10) | 2 |
| cheap (string or exported-vtable anchor: 12, 13, 14, 15, 17, 25) | 6 |
| real reverse engineering (16, 18 to 24) | 8 |
| of which return-address sites | 3 |
| reflected natives among the 29 | 0 |

Return-address caveat: the detour sees the caller only if clang kept the call. If UpdateItem_ServerInternal
or Create_ServerInternal was inlined into its save-apply caller on Linux, the check has no site to match
and needs a different hook point. Unknown until the bodies are found.

## Required by feature

- Raw DataTable mods: rows 11 and 26, both proven. Row 12 is optional and located.
- Item mods (new item rows): row 11 plus core. Rows 18 to 25 are PalItemModLoader::SetupHooks, which
  only exists so a save that still references an uninstalled custom item does not crash. It sits in a
  try/catch on Windows; the loader keeps going without it.
- Pal and Blueprint mods: rows 13, 14, 15, 16 plus core. Two anchors, one string, one blind.
- Custom spawns: row 17. One anchor.

## Scope note

Runs 16 to 22 used mirrored logic. Run 33 (2026-09-19 22:41) ran PalSchema's own PalRawTableLoader,
PropertyHelper and UDataTable::AddRow path for the same edit and insertion: 1 updated, 1 added, 0 errors,
engine-verified, removal clean. The raw-table slice is integrated; see README runs 23 to 33. Before the
editor accepts wider input, write_value needs the integrality and range checks PalSchema does in
ValidateJsonValueType; today any JSON number is narrowed to int or byte types unchecked.

## What the reflection column bought

Nothing for these 25, everything for the future. The `_ServerInternal` suffix is not a UFUNCTION
marker in Palworld. But the dump is a list of 19,692 functions UE4SS can hook by name with
RegisterHook and no signature at all, 9,651 of them Palworld's own. Any feature that can be
expressed against those needs no Ghidra. Caveat: small bodies get inlined into their exec thunk
(PalItemSlot::GetItemId is the thunk), so the dump gives hook targets, not body addresses.

## Update-proofness

None of this survives a Pocketpair build blindly. Vtable slots move (the port hardcodes
ProcessEvent at 0x268 and InitGameState at 0x740 for PalServer specifically, with a consensus sweep
as fallback), reflected names are the most stable, string xrefs next, raw code addresses last.
Every layer here is per-build; the difference is minutes versus hours to redo.
