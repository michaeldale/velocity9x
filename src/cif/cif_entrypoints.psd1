# The exports of ATI's ATI3DCIF.DLL, the one statement of them for
# scripts\build-cif.ps1 (docs\plans\ati-3dcif-wrapper.md).
#
# From the export tables of ATI's 4.11.2692 (the copy on A8U4I5) and
# 4.12.2696, which carry the same 39 names: each entry point twice, as the
# __stdcall function _ATI3DCIF_<Name>@<Bytes> in .text and as
# ATI3DCIF_<Name>_lib, a pointer to that function in .data. ATI3DCIF.H
# (SDK 4.1) declares the second as "extern function_name##_t
# function_name##_lib" so that GetProcAddress needs no decorated name; the
# demos look up only the _lib names. The 39th export is a C++ operator of
# ATI's own and is not reproduced.
#
# Bytes is the argument size from the decorated name. Return is u32 for
# every entry: C3D_EC, or the C3D_HRC handle for ContextCreate.
# RenderIndexedPrim is exported by 4.10.2640 and later but is not in the
# SDK 4.1 header; its arguments are unknown beyond their 12 bytes.
@{
    Entries = @(
        @{ Name = 'Init';                  Bytes = 0  }
        @{ Name = 'Term';                  Bytes = 0  }
        @{ Name = 'GetInfo';               Bytes = 4  }
        @{ Name = 'TextureReg';            Bytes = 8  }
        @{ Name = 'TextureUnreg';          Bytes = 4  }
        @{ Name = 'TexturePaletteCreate';  Bytes = 12 }
        @{ Name = 'TexturePaletteDestroy'; Bytes = 4  }
        @{ Name = 'TexturePaletteAnimate'; Bytes = 16 }
        @{ Name = 'LoadObject';            Bytes = 12 }
        @{ Name = 'ContextCreate';         Bytes = 0  }
        @{ Name = 'ContextDestroy';        Bytes = 4  }
        @{ Name = 'ContextSetState';       Bytes = 12 }
        @{ Name = 'RenderBegin';           Bytes = 4  }
        @{ Name = 'RenderEnd';             Bytes = 0  }
        @{ Name = 'RenderSwitch';          Bytes = 4  }
        @{ Name = 'RenderPrimStrip';       Bytes = 8  }
        @{ Name = 'RenderPrimList';        Bytes = 8  }
        @{ Name = 'RenderPrimMesh';        Bytes = 12 }
        @{ Name = 'RenderIndexedPrim';     Bytes = 12 }
    )
}
