/*
 * Tests for the INF subset V9XUPD.EXE applies (update_inf.c).
 *
 * The fixture is the generated Matrox INF of 2026-10-09
 * (build\floppy\MATROX\MATROX\VELOCITY9X.INF), cut to one model and with
 * the LogConfig model the S3 and VBE INFs carry added. Every generated INF
 * is also planned in full by build-active-package.ps1 through
 * tools\release\v9xinfplan.c; this file holds the rules to a table.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/update_inf.h"

static unsigned int inf_failures = 0u;

#define INFCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++inf_failures; \
    } \
} while (0)

static const char inf_text[] =
    "; Velocity9x Windows 98SE bring-up package\r\n"
    "[Version]\r\n"
    "Signature=\"$CHICAGO$\"\r\n"
    "Class=DISPLAY\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "Velocity9x=Velocity9x.Models\r\n"
    "\r\n"
    "[Velocity9x.Models]\r\n"
    "\"Velocity9x Matrox Millennium MGA-2064W\"=V9x.Install.mga2064w,PCI\\VEN_102B&DEV_0519\r\n"
    "\"Velocity9x, any VESA adapter\"=Velocity9x.Install.Manual\r\n"
    "\r\n"
    "[DestinationDirs]\r\n"
    "DefaultDestDir=11\r\n"
    "Velocity9x.Copy=11\r\n"
    "\r\n"
    "[V9x.Install.mga2064w]\r\n"
    "CopyFiles=Velocity9x.Copy\r\n"
    "DelReg=Velocity9x.Previous\r\n"
    "AddReg=Velocity9x.Registry,V9x.Registry.mga2064w\r\n"
    "\r\n"
    "[Velocity9x.Install.Manual]\r\n"
    "CopyFiles=Velocity9x.Copy\r\n"
    "DelReg=Velocity9x.Previous\r\n"
    "AddReg=Velocity9x.Registry\r\n"
    "LogConfig=Velocity9x.LogConfig\r\n"
    "\r\n"
    "[Velocity9x.Copy]\r\n"
    "v9xdisp.drv,,,12\r\n"
    "v9xmini.vxd,,,12\r\n"
    "glide2x.dll,,,40 ; a 3dfx card's own is kept\r\n"
    "v9xupd.exe,,,12\r\n"
    "\r\n"
    "[Velocity9x.Previous]\r\n"
    "HKR,,Ver\r\n"
    "HKR,DEFAULT\r\n"
    "HKR,MODES\r\n"
    "\r\n"
    "[Velocity9x.Registry]\r\n"
    "HKR,,Ver,,4.0\r\n"
    "HKR,,V9xFamily,,\"matrox\"\r\n"
    "HKR,DEFAULT,vdd,,\"*vdd,*vflatd\"\r\n"
    "HKR,\"MODES\\4\\640,480\",drv,,vga.drv\r\n"
    "HKCR,CLSID\\{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1},,,\"Velocity9x Settings Page\"\r\n"
    "HKLM,\"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved\",{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1},,\"Velocity9x Settings Page\"\r\n"
    "HKLM,Software\\Microsoft\\Windows\\CurrentVersion\\Run,V9xSyncModes,,\"rundll32.exe v9xsetp.dll,V9xSyncModes\"\r\n"
    "\r\n"
    "[V9x.Registry.mga2064w]\r\n"
    "HKR,\"MODES\\8\\640,480\",,,60\r\n"
    "\r\n"
    "[Velocity9x.LogConfig]\r\n"
    "ConfigPriority=HARDWIRED\r\n"
    "IOConfig=3B0-3BB\r\n";

static struct v9x_inf_plan plan;

static void test_plan(void)
{
    const struct v9x_inf_op *op;

    INFCHECK(v9x_inf_plan(inf_text, (v9x_u32)strlen(inf_text),
                          "v9x.install.MGA2064W", &plan));
    /* 4 copies, 3 deletions, 7 + 1 additions, in that order. */
    INFCHECK(plan.count == 15u);
    INFCHECK(plan.copies == 4u);

    op = &plan.ops[0];
    INFCHECK(op->kind == V9X_INF_OP_COPY && strcmp(op->name, "v9xdisp.drv") == 0 &&
             op->copy_flags == V9X_INF_COPY_ALWAYS);
    op = &plan.ops[2];
    INFCHECK(op->kind == V9X_INF_OP_COPY && strcmp(op->name, "glide2x.dll") == 0 &&
             op->copy_flags == V9X_INF_COPY_IF_NEWER);

    op = &plan.ops[4];
    INFCHECK(op->kind == V9X_INF_OP_DEL_VALUE && op->root == V9X_INF_ROOT_HKR &&
             op->key[0] == '\0' && strcmp(op->name, "Ver") == 0);
    op = &plan.ops[5];
    INFCHECK(op->kind == V9X_INF_OP_DEL_KEY && strcmp(op->key, "DEFAULT") == 0);

    op = &plan.ops[8];
    INFCHECK(op->kind == V9X_INF_OP_SET_STRING && strcmp(op->name, "V9xFamily") == 0 &&
             strcmp(op->data, "matrox") == 0);
    op = &plan.ops[9];
    INFCHECK(strcmp(op->key, "DEFAULT") == 0 && strcmp(op->data, "*vdd,*vflatd") == 0);
    op = &plan.ops[10];
    INFCHECK(strcmp(op->key, "MODES\\4\\640,480") == 0 && strcmp(op->name, "drv") == 0 &&
             strcmp(op->data, "vga.drv") == 0);
    op = &plan.ops[11];
    INFCHECK(op->root == V9X_INF_ROOT_HKCR && op->name[0] == '\0' &&
             strcmp(op->data, "Velocity9x Settings Page") == 0);
    op = &plan.ops[12];
    INFCHECK(op->root == V9X_INF_ROOT_HKLM &&
             strcmp(op->name, "{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}") == 0);
    op = &plan.ops[13];
    INFCHECK(strcmp(op->data, "rundll32.exe v9xsetp.dll,V9xSyncModes") == 0);
    op = &plan.ops[14];
    INFCHECK(strcmp(op->key, "MODES\\8\\640,480") == 0 && op->name[0] == '\0' &&
             strcmp(op->data, "60") == 0);

    /* LogConfig is accepted and contributes nothing. */
    INFCHECK(v9x_inf_plan(inf_text, (v9x_u32)strlen(inf_text),
                          "Velocity9x.Install.Manual", &plan));
    INFCHECK(plan.count == 14u);

    INFCHECK(!v9x_inf_plan(inf_text, (v9x_u32)strlen(inf_text),
                           "V9x.Install.absent", &plan));
    INFCHECK(plan.error[0] != '\0');
}

/* Replace the first occurrence of from with to in the fixture and plan. */
static v9x_u16 plan_variant(const char *from, const char *to,
                            const char *section)
{
    static char text[sizeof(inf_text) + 256];
    const char *at = strstr(inf_text, from);
    size_t head;

    if (at == 0) {
        printf("FAIL fixture lacks %s\n", from);
        ++inf_failures;
        return V9X_TRUE;
    }
    head = (size_t)(at - inf_text);
    memcpy(text, inf_text, head);
    strcpy(text + head, to);
    strcat(text, at + strlen(from));
    return v9x_inf_plan(text, (v9x_u32)strlen(text), section, &plan);
}

static void test_refusals(void)
{
    static const char model[] = "V9x.Install.mga2064w";

    /* Directives outside the subset. */
    INFCHECK(!plan_variant("DelReg=Velocity9x.Previous\r\n",
                           "DelReg=Velocity9x.Previous\r\nUpdateInis=x\r\n",
                           model));
    INFCHECK(!plan_variant("CopyFiles=Velocity9x.Copy\r\n",
                           "CopyFiles=@v9xdisp.drv\r\n", model));
    /* Destinations other than the system directory. */
    INFCHECK(!plan_variant("Velocity9x.Copy=11\r\n", "Velocity9x.Copy=10\r\n",
                           model));
    /* Copy: a rename, an unknown flag, a long name, a path. */
    INFCHECK(!plan_variant("v9xmini.vxd,,,12", "v9xmini.vxd,old.vxd,,12", model));
    INFCHECK(!plan_variant("v9xmini.vxd,,,12", "v9xmini.vxd,,,16", model));
    INFCHECK(!plan_variant("v9xmini.vxd,,,12", "velocity9x.vxd,,,12", model));
    INFCHECK(!plan_variant("v9xmini.vxd,,,12", "..\\v9xmini.vxd,,,12", model));
    /* Registry: another root, a typed value, a string substitution, the
     * driver key itself, an unterminated quote, too many fields. */
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKU,,Ver,,4.0", model));
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKR,,Ver,0x10001,4", model));
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKR,,Ver,,%Version%", model));
    INFCHECK(!plan_variant("HKR,DEFAULT\r\n", "HKR,\r\n", model));
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKR,,Ver,,\"4.0", model));
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKR,,Ver,,4.0,extra", model));
    INFCHECK(!plan_variant("HKR,,Ver,,4.0", "HKR,,Ver", model));
    /* A list naming a section that is not there. */
    INFCHECK(!plan_variant("AddReg=Velocity9x.Registry,V9x.Registry.mga2064w",
                           "AddReg=Velocity9x.Registry,V9x.Registry.none",
                           model));
    /* Comments: a ';' inside quotes is data, outside it ends the line. */
    INFCHECK(plan_variant("HKR,,Ver,,4.0", "HKR,,Ver,,\"a;b\" ; note", model));
    INFCHECK(strcmp(plan.ops[7].data, "a;b") == 0);
}

static void test_find_section(void)
{
    char section[64];
    v9x_u32 length = (v9x_u32)strlen(inf_text);

    /* A8U4I5's Matrox key: an InfSection the new INF no longer has, found
     * by its MatchingDeviceId instead, without case. */
    INFCHECK(v9x_inf_find_section(inf_text, length, "pci\\ven_102b&dev_0519",
                                  "Velocity9x.Install", section,
                                  sizeof(section)));
    INFCHECK(strcmp(section, "V9x.Install.mga2064w") == 0);
    /* The ID wins over a still-present old section name. */
    INFCHECK(v9x_inf_find_section(inf_text, length, "PCI\\VEN_102B&DEV_0519",
                                  "Velocity9x.Install.Manual", section,
                                  sizeof(section)));
    INFCHECK(strcmp(section, "V9x.Install.mga2064w") == 0);
    /* No ID (a manual-select install): the old name, if it is still here. */
    INFCHECK(v9x_inf_find_section(inf_text, length, "",
                                  "Velocity9x.Install.Manual", section,
                                  sizeof(section)));
    INFCHECK(strcmp(section, "Velocity9x.Install.Manual") == 0);
    /* Neither: refused. A prefix of a listed ID is not that ID. */
    INFCHECK(!v9x_inf_find_section(inf_text, length, "PCI\\VEN_102B",
                                   "Velocity9x.Install", section,
                                   sizeof(section)));
    INFCHECK(!v9x_inf_find_section(inf_text, length, "PCI\\VEN_5333&DEV_8A01",
                                   "", section, sizeof(section)));
}

unsigned int v9x_run_update_inf_tests(void)
{
    test_plan();
    test_find_section();
    test_refusals();
    return inf_failures;
}
