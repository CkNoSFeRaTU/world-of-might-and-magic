#pragma once

class Blob;

struct FactionTable {
    void Initialize(const Blob &factions);

    char relations[89][89];
};

extern FactionTable *pFactionTable;
