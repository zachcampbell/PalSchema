# Ghidra results for PalServer-Linux-Shipping v1.0.5.102999 (2026-09-20)

Ghidra 12.1.3 headless (/opt/ghidra, Java 21), project /tmp/palhook-ghidra/PalServer, full auto-analysis of the
non-PIE binary took about 2.5 hours (05:19 to 07:53). Scripts in tools/ghidra/: FindPalTargets.java (anchor
search, output targets-run1.txt) and DecompileList.java (decompiles the addresses listed in decompile-list.txt
into decomp/<addr>.c with callers and callees). Rerun with:

    /opt/ghidra/support/analyzeHeadless /tmp/palhook-ghidra PalServer -process PalServer-Linux-Shipping-v1.0.5.102999 \
      -noanalysis -readOnly -scriptPath /tmp/palhook-ghidra/scripts -postScript DecompileList.java

## Located (wired into palslice/shim/PalSignatures.cpp with prologue checks, run 79)

| function | address | how it was identified |
|---|---|---|
| UWorld::CleanupWorld(bool, bool, UWorld*) | 0xab1e020 | 35 bytes: `add dword [0xc2def88], 1` (CleanupWorldGlobalTag), call 0xab23e50, clear bit 0x40 at +0x13b. 0xab23e50 is CleanupWorldInternal: compares UWorld+0x70c (MemberVariableLayout CleanupWorldTag) with the same global and recurses into sub-level worlds; found by scanning 26.8M instructions for [reg+0x70c] with a compare. Caller 0xa806240 is the UWorld destructor path. |
| UStruct::StaticLink(bool) | 0x7a39e60 | 75 bytes: FArchive constructor 0x7943a70, stores vtable 0x1a3bbb8 (FArchive) on the stack, calls this->vtable[0x2d0] (Link: 5.1 baseline 0x2c8 + 8), FArchive destructor 0x7953ad0. 31 callers, all StaticClass registration. |
| UClass::AssembleReferenceTokenStream(bool bForce) | 0x7ad40f0 | sole caller of the cold stub 0x7ad4470 that formats "AssembleReferenceTokenStream for %s called on a non-game thread while GC is not locked."; tests ClassFlags(+0xd4) & 0x400000 (CLASS_TokenStreamAssembled), bForce clears it, token stream at +0x218. |
| FPakPlatformFile::GetPakFolders(const TCHAR*, TArray<FString>&) | 0x9dfb450 | the one .text user of UTF-16 "%sPaks/" (0x7a57f2); appends three 16-byte FStrings to the array in rsi. Runs at boot before mods start, so the hook is installed but never observed firing. |

Also seen on the way: UClass::GetDefaultObjectName 0x7a41f60 ("Default__" + name); 0x7bbaa80 (2124 bytes,
called by StaticConstructObject_Internal 0x7bba630 and 16 others, uses GetDefaultObjectName, FName::ToString,
a mutex and the 3425-byte 0x7bd42f0) is StaticAllocateObject or its CDO-aware sibling, and 0x7bbef00 /
0x7bbf360 / 0x7bbf760 (its callers that also call StaticLink) are the CreateDefaultObject family. No
out-of-line UClass::GetDefaultObject(bool) exists on Linux: FindCdoCreate.java found 390 inlined sites of
`cmp [class+0x110], 0` and the creating branch calls vtable slot 0x3f8 (e.g. 0x46cb68e), which is
UClass::CreateDefaultObject 0x7a41c10 (baseline 0x3f0 + 8). The shim adapter calls that slot (runs 80/81).
PurgeClass: baseline 0x3c0 -> Linux 0x3c8 = index 121; the BlueprintGeneratedClass vtable there holds
UBlueprintGeneratedClass::PurgeClass 0xa15f310 (calls UClass::PurgeClass 0x7a44e00). PalSchema's 120 fixed.

## Item save-safety set, partial (2026-09-20 12:20, runs 90 and Ghidra passes FindByOffsets / FindStrings)

Reflection (probe v0.52 propoffset, run 90): UPalItemSlot ItemId at +0x12c (40-byte struct), StackCount +0x154,
SlotIndex +0x118, class size 0x1d0; UBlueprintGeneratedClass ComponentTemplates +0x280 (TArray), Timelines +0x290,
SimpleConstructionScript +0x2b0.

| target | status | evidence |
|---|---|---|
| UPalDynamicItemWorldSubsystem::ApplyWorldSaveData | LOCATED 0x71ad770 | owns the "Failed to Create DynamicItemData from save data. %s" stub (0x71ae260) and calls the validator with the "ApplyWorldSaveData.BeforeCreate" / ".AfterApply" contexts |
| UPalDynamicItemWorldSubsystem::Create_ServerInternal | LOCATED 0x71ac3c0 | the call at 0x71ad8fa whose null result triggers that stub; rdi=this rsi=FPalDynamicItemId* rdx=FName staticId rcx=create param, matching PalSchema's detour prototype. Return-address site for the hook: 0x71ad8ff |
| ValidateDynamicItemSaveData | LOCATED 0x71ada90 | called as (rdi=save entry, rsi=UPalDynamicItemDataBase* or null, rdx=item id manager, rcx=const TCHAR* context), behind a global bool at 0xbd538d8 (validity-check switch); formats "DynamicItemData StaticId Mismatch. Context=%s, %s, %s" via 0x71dd7e0. PalSchema's detour returns true and ignores the arguments, so its FString& fourth parameter being a raw TCHAR* here is harmless |
| UPalItemContainer::Get(int32) | located 0x72bb6c0 (helper) | the "ItemSlotArray.Num()" check; 100+ callers |
| UPalItemSlot::UpdateItem_ServerInternal | NOT FOUND | offset-keyed scans (stores to +0x154 / +0x12c) return unrelated classes sharing those offsets; no string |
| UPalItemContainer::ApplySaveData | NOT FOUND | needs UpdateItem first (it is the return-address site after that call) |
| ValidateWorldSaveDynamicItemStaticIds | NOT FOUND | the "[Load]...InvalidDynamicItemStaticId" tag in UPalSaveGameManager::PalLoadGameFromSlot (0x75a1720) is preceded by a cast check, not by a four-argument validator; 0x77a52f0 there is a guarded singleton getter |
| FPalPlayerRecordDataRepInfoArrayThreadSafe_IntVal::ApplyDataMap, CraftItemCount return site | NOT FOUND | the "CraftItemCount" FName literal is referenced only from data |

PalItemModLoader::SetupHooks throws on the first missing signature, so nothing from this table is wired: the
set only matters for a save that still references an uninstalled custom item, and a half-installed set would
be worse than none.

UBlueprintGeneratedClass::FindComponentTemplateByName: no function in the binary touches ComponentTemplates
(+0x280/+0x288) together with NamePrivate (+0x18) in under 200 bytes; its callers are editor-side, so it was
most likely stripped. Nothing in this PalSchema tree calls the helper that needs it (BPGeneratedClassHelper's
FindComponentTemplateByName and GetGeneratedClassesHierarchy have no callers), so it is closed as unused.

## Still open

- UBlueprintGeneratedClass::FindComponentTemplateByName: no string, no reflection anchor. Needs the
  ComponentTemplates offset (not in MemberVariableLayout.ini; read it from reflection at runtime) and a scan for
  a small function looping that TArray comparing UObject::NamePrivate (+0x18) with an FName argument.
- The item save-safety set (UPalItemSlot::UpdateItem_ServerInternal, UPalItemContainer::ApplySaveData,
  UPalDynamicItemWorldSubsystem::Create_ServerInternal / ApplyWorldSaveData, ValidateWorldSaveDynamicItemStaticIds,
  ValidateDynamicItemSaveData, FPalPlayerRecordDataRepInfoArrayThreadSafe_IntVal::ApplyDataMap,
  CraftItemCount_ApplyDataMapReturn): Pal code with no strings. PalSchema's item loader wraps them in try/catch
  and keeps going; they only matter for a save that still references an uninstalled custom item.
- AsyncTask: replaced on Linux by the ProcessEvent game-thread drain (Async.cpp).
