#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "Memory.h"
#include "Object.h"
#include "Table.h"
#include "Value.h"

#define TABLE_MAX_LOAD 0.75

void TableInit(KTN_Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

static KTN_TableEntry* FindEntry(KTN_TableEntry* entries, int capacity, KTN_ObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    KTN_TableEntry* tombstone = NULL;

    for(;;) {
        KTN_TableEntry* entry = &entries[index];

        if (entry->Key == NULL) {
            if (IS_NULL(entry->value)) {
                return (tombstone != NULL) ? tombstone : entry;
            }
            else {
                if (tombstone == NULL) tombstone = entry;
            }
        }
        else if (entry->Key == key) {
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

bool TableGet(KTN_Table* table, KTN_ObjString* key, KTN_Value* value) {
    if (table->count == 0) return false;

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    if (entry->Key == NULL) return false;

    *value = entry->value;

    return true;
}

uint8_t TableGetFlags(KTN_Table* table, KTN_ObjString* key) {
    if (table->count == 0) return 0;

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);

    if (entry->Key == NULL) return 0;

    return entry->flags;
}

bool TableContains(KTN_Table* table, KTN_ObjString* key) {
    if (table->count == 0) return false;

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    return (entry->Key != NULL);
}

static void AdjustCapacity(KTN_VM* vm, KTN_Table* table, int capacity) {
    KTN_TableEntry* entries = ALLOCATE(KTN_TableEntry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].Key = NULL;
        entries[i].value = NULL_VALUE;
        entries[i].flags = 0;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        KTN_TableEntry* entry = &table->entries[i];
        if (entry->Key == NULL) continue;

        KTN_TableEntry* dest = FindEntry(entries, capacity, entry->Key);
        dest->Key = entry->Key;
        dest->value = entry->value;
        dest->flags = entry->flags;
        table->count++;
    }

    FREE_ARRAY(KTN_TableEntry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool TableSet(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        AdjustCapacity(vm, table, capacity);
    }

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    
    bool isNewKey = (entry->Key == NULL);

    if (isNewKey && IS_NULL(entry->value)) table->count++;

    entry->Key = key;
    entry->value = value;
    if (isNewKey)
        entry->flags = 0;
    
    return isNewKey;
}

bool TableSetNonexistent(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        AdjustCapacity(vm, table, capacity);
    }
    
    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    bool isNewKey = (entry->Key == NULL);
    
    if (isNewKey && IS_NULL(entry->value)) { 
        entry->Key = key;
        entry->value = value;
        entry->flags = 0;
        
        table->count++;
    }
    
    return isNewKey;
}

bool TableSetFlagged(KTN_VM* vm, KTN_Table* table, KTN_ObjString* key, KTN_Value value, uint8_t flags) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        AdjustCapacity(vm, table, capacity);
    }

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    
    bool isNewKey = (entry->Key == NULL);

    if (isNewKey && IS_NULL(entry->value)) table->count++;

    entry->Key = key;
    entry->value = value;
    entry->flags = flags;
    
    return isNewKey;
}

bool TableSetFlags(KTN_Table* table, KTN_ObjString* key, uint8_t flags) {
    if (table->count == 0) return false;

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    
    if (entry->Key == NULL) return false;
    
    entry->flags |= flags;
    return true;
}

bool TableDelete(KTN_Table* table, KTN_ObjString* key) {
    if (table->count == 0) return false;

    KTN_TableEntry* entry = FindEntry(table->entries, table->capacity, key);
    if (entry->Key == NULL) return false;

    entry->Key = NULL;
    entry->value = BOOL_VALUE(true);
    entry->flags = 0;
    return true;
}

void TableAddAll(KTN_VM* vm, KTN_Table* from, KTN_Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        KTN_TableEntry* entry = &from->entries[i];
        if (entry->Key != NULL) {
            TableSetFlagged(vm, to, entry->Key, entry->value, entry->flags);
        }
    }
}

KTN_ObjString* TableFindString(KTN_Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0)  return NULL;

    uint32_t index = hash & (table->capacity - 1);

    for (;;) {
        KTN_TableEntry* entry = &table->entries[index];
        if (entry->Key == NULL) {
            if (IS_NULL(entry->value))  return NULL;
        }
        else if (entry->Key->length == length &&
                 entry->Key->hash == hash &&
                 memcmp(entry->Key->chars, chars, length) == 0) {
                    return entry->Key;
        }

        index = (index + 1) & (table->capacity - 1);
    }
}

void TableRemoveWhite(KTN_VM* vm, KTN_Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        KTN_TableEntry* entry = &table->entries[i];
        if (entry->Key != NULL && !entry->Key->object.isMarked)
            TableDelete(table, entry->Key);
    }
}

void TableMark(KTN_VM* vm, KTN_Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        KTN_TableEntry* entry = &table->entries[i];
        KTN_MemoryMarkObject(vm, (KTN_Object*)entry->Key);
        KTN_MemoryMarkValue(vm, entry->value);
    }
}

void TableFree(KTN_VM* vm, KTN_Table* table) {
    FREE_ARRAY(KTN_TableEntry, table->entries, table->capacity);
    TableInit(table);
}
