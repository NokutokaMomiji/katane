#ifndef KATANE_TABLE_H
#define KATANE_TABLE_H

#include "Common.h"
#include "Value.h"

#define KTN_TABLE_ENTRY_CONST 1
#define KTN_TABLE_ENTRY_FINAL 2
#define KTN_TABLE_ENTRY_PRIVATE 4
typedef struct {
    KTN_ObjString* Key;
    KTN_Value value;
    uint8_t flags;
} KTN_TableEntry;

typedef struct {
    int count;
    int capacity;
    KTN_TableEntry* entries;
} KTN_Table;

void TableInit(KTN_Table* table);
bool TableSet(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value);
bool TableSetNonexistent(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value);
bool TableSetFlags(KTN_Table* table, KTN_ObjString* key, uint8_t flags);
bool TableSetFlagged(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value, uint8_t flags);
bool TableGet(KTN_Table* table, KTN_ObjString* key, KTN_Value* value);
uint8_t TableGetFlags(KTN_Table* table, KTN_ObjString* key);
bool TableContains(KTN_Table* table, KTN_ObjString* key);
bool TableDelete(KTN_Table* table, KTN_ObjString* key);
void TableAddAll(KTN_VM* vm, KTN_Table* from, KTN_Table* to);
KTN_ObjString* TableFindString(KTN_Table* table, const char* chars, int length, uint32_t hash);
void TableRemoveWhite(KTN_VM* vm, KTN_Table* table);
void TableMark(KTN_VM* vm, KTN_Table* table);
void TableFree(KTN_VM* vm, KTN_Table* table);

#endif