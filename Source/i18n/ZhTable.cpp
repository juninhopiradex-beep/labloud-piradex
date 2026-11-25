// Regista a tabela de traducoes deste plugin (PT / EN / ZH) no modulo de idioma Piradex.
#include "PiradexLanguage.h"

namespace
{
const piradex::i18n::Entry kTable[] = {
#include "ZhTable.inc"
};
}

namespace piradex::i18n
{
// chamado no construtor do processador (registo explicito: o linker nao descarta a tabela)
void registerPluginTable()
{
    static bool done = false;
    if (done) return;
    done = true;
    registerTable (kTable, (int) (sizeof (kTable) / sizeof (kTable[0])));
}
}
