#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gelf.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "gettext.h"
#include "processing_abi_features.h"

#define _(x) gettext(x)

// DT_RELR constant (not always defined in older headers)
#ifndef DT_RELR
#define DT_RELR 36
#endif

#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif

#ifndef DT_RELRENT
#define DT_RELRENT 37
#endif

// LoongArch-specific TLSDESC dynamic tags
// These may vary; adjust based on actual LoongArch ABI specs
#ifndef DT_LARCH_TLSDESC_PLT
#define DT_LARCH_TLSDESC_PLT 0x70000001
#endif

#ifndef DT_LARCH_TLSDESC_GOT
#define DT_LARCH_TLSDESC_GOT 0x70000002
#endif

// Global structure to track ABI feature usage per file
struct abi_feature_usage {
    const char *path;
    abi_feature_flags_t features;
    struct abi_feature_usage *next;
};

static struct abi_feature_usage *g_abi_feature_list = NULL;
static bool g_has_abi_features = false;

/////////////////////////////////////////////////////////////////////////////

void check_abi_features(struct sl_elf_ctx *ctx, Elf *e)
{
    abi_feature_flags_t features = ABI_FEATURE_NONE;
    
    // Scan dynamic section for DT_RELR and TLSDESC tags
    Elf_Scn *scn = NULL;
    while ((scn = elf_nextscn(e, scn)) != NULL) {
        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) != &shdr) {
            continue;
        }
        
        if (shdr.sh_type != SHT_DYNAMIC) {
            continue;
        }
        
        Elf_Data *data = NULL;
        while ((data = elf_getdata(scn, data)) != NULL) {
            size_t entries = data->d_size / sizeof(Elf64_Dyn);
            Elf64_Dyn *dyn = (Elf64_Dyn *)data->d_buf;
            
            for (size_t i = 0; i < entries; i++) {
                switch (dyn[i].d_tag) {
                case DT_RELR:
                case DT_RELRSZ:
                case DT_RELRENT:
                    features |= ABI_FEATURE_RELR;
                    break;
                    
                case DT_LARCH_TLSDESC_PLT:
                case DT_LARCH_TLSDESC_GOT:
                    features |= ABI_FEATURE_TLSDESC;
                    break;
                    
                case DT_NULL:
                    // End of dynamic section
                    goto done_scanning;
                    
                default:
                    break;
                }
            }
        }
    }
    
done_scanning:
    // If any features found, record them
    if (features != ABI_FEATURE_NONE) {
        g_has_abi_features = true;
        
        // Check if this file is already in the list
        struct abi_feature_usage *existing = g_abi_feature_list;
        while (existing != NULL) {
            if (strcmp(existing->path, ctx->path) == 0) {
                // Update existing entry
                existing->features |= features;
                return;
            }
            existing = existing->next;
        }
        
        // Add new entry
        struct abi_feature_usage *new_entry = malloc(sizeof(struct abi_feature_usage));
        if (new_entry == NULL) {
            return;
        }
        
        new_entry->path = strdup(ctx->path);
        if (new_entry->path == NULL) {
            free(new_entry);
            return;
        }
        
        new_entry->features = features;
        new_entry->next = g_abi_feature_list;
        g_abi_feature_list = new_entry;
    }
}

void abi_features_print_final_report(void)
{
    if (!g_has_abi_features) {
        printf(_(
            "\n\x1b[32m * \x1b[mNo novel ABI features found on your system.\n\n"
        ));
        return;
    }
    
    printf(_("\n\x1b[1;33m# Novel ABI Features Report\x1b[m\n\n"));
    
    // Print summary by feature type
    int relr_count = 0, tlsdesc_count = 0;
    
    struct abi_feature_usage *entry = g_abi_feature_list;
    while (entry != NULL) {
        if (entry->features & ABI_FEATURE_RELR) relr_count++;
        if (entry->features & ABI_FEATURE_TLSDESC) tlsdesc_count++;
        entry = entry->next;
    }
    
    printf(_("Feature usage summary:\n"));
    if (relr_count > 0) {
        printf(_("  \x1b[36mDT_RELR\x1b[m  (relative relocations): %d file(s)\n"), relr_count);
    }
    if (tlsdesc_count > 0) {
        printf(_("  \x1b[36mTLSDESC\x1b[m (TLS descriptors): %d file(s)\n"), tlsdesc_count);
    }
    
    printf(_("\nDetailed file list:\n"));
    
    entry = g_abi_feature_list;
    while (entry != NULL) {
        printf("  %s:", entry->path);
        
        bool first = true;
        if (entry->features & ABI_FEATURE_RELR) {
            printf(" DT_RELR");
            first = false;
        }
        if (entry->features & ABI_FEATURE_TLSDESC) {
            printf("%sTLSDESC", first ? " " : ", ");
        }
        printf("\n");
        
        entry = entry->next;
    }
    
    printf(_(
        "\n"
        "\x1b[32m * \x1b[mThese files use modern ABI features. Your toolchain and\n"
        "   runtime should support them.\n"
        "\n"
    ));
    
    // Cleanup
    while (g_abi_feature_list != NULL) {
        struct abi_feature_usage *next = g_abi_feature_list->next;
        free((void *)g_abi_feature_list->path);
        free(g_abi_feature_list);
        g_abi_feature_list = next;
    }
}
