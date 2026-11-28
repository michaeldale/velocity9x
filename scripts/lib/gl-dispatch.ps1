# Generates the OpenGL 1.1 dispatch table from the manifest.
#
# src\opengl\gl_entrypoints.psd1 is the one statement of the 336 slots of
# GLCLTPROCTABLE.glDispatchTable: slot, name, return type, parameter list,
# cross-checked against ReactOS icd.h and Mesa gldrv.h on 2026-09-26. The ICD
# needs a typed function pointer, a typed stub and a table entry per slot, and
# the host test needs the names in order; generating all of it from the one
# file is what keeps a hand-typed copy from drifting (the same reason
# scripts\lib\family-matrix.ps1 exists). Dot-sourced by the host build scripts
# and by scripts\build-opengl-icd.ps1.
#
# The includer defines, before including the generated header:
#   V9X_GL_API           the calling convention of a slot (__stdcall)
#   V9X_GL_STUB_HOOK(n)  what a stub does before returning zero; the ICD counts
#                        the slot, the host test records it
# and, with V9X_GL_DEFINE_CENSUS:
#   V9X_GL_CENSUS_HOOK(n)    what a census forwarder does before calling on
#   V9X_GL_CENSUS_TARGET(n)  the V9X_GL_PROC it calls on, with the arguments
# and gets:
#   V9X_GL_SLOT_COUNT, V9X_GL_SLOT_NAMES (an initialiser of 336 strings),
#   V9X_GL_PFN_<name> typedefs, v9x_gl_stub_<name> static definitions when
#   V9X_GL_DEFINE_STUBS is defined, and V9X_GL_DISPATCH_INIT (an initialiser
#   of 336 V9X_GL_PROC values in slot order). With V9X_GL_DEFINE_CENSUS also
#   v9x_gl_census_<name>, which runs the hook and forwards every argument and
#   the result, and V9X_GL_CENSUS_INIT, their table in slot order: the ICD
#   counts the calls an implemented slot gets by putting its forwarder in
#   the table in front of it.
#
# Display lists (docs\plans\opengl-display-lists-and-texgen.md): with
# V9X_GL_DEFINE_SAVE as well, each forwarder of a slot that
# src\opengl\gl_dlist_rules.psd1 compiles first offers the call to
# v9x_gl_save_<name>, which copies it into the list being compiled, and
# returns without executing when the save says so. The includer defines:
#   V9X_GL_SAVING()                non-zero while a list is being compiled
#   V9X_GL_DLIST_NODE(slot, words) a node's argument words, or null
#   V9X_GL_DLIST_COPY(to, from, n) a byte copy
#   V9X_GL_DLIST_COMPILE_ONLY()    non-zero when the call is not executed
#   V9X_GL_DLIST_UNCOMPILED(slot)  a Deferred slot reached while compiling
#   V9X_GL_REPLAY_TARGET(slot)     the V9X_GL_PROC replay calls
#   V9X_GL_REPLAY_SPECIAL(slot, args, words)  a Special slot's node
# and v9x_gl_save_<name> for each Special slot, after the header. It gets
# v9x_gl_save_<name> for the compiled slots and v9x_gl_dlist_replay_node,
# which calls a node's slot with the arguments it holds.

# The C types a parameter can have, as the save and replay code moves them:
# one word for the integers and GLfloat, two for GLdouble.
$script:V9xGlIntegerTypes = @('GLenum', 'GLint', 'GLuint', 'GLsizei',
    'GLbitfield', 'GLboolean', 'GLbyte', 'GLubyte', 'GLshort', 'GLushort')

function Get-V9xGlParams {
    param([Parameter(Mandatory = $true)]$Entry)

    $result = @()
    $params = [string]$Entry.Params
    if ($params -eq 'void') {
        return ,$result
    }
    foreach ($p in ($params -split ',')) {
        $text = $p.Trim()
        $pointer = $text.Contains('*')
        $const = $text.StartsWith('const ')
        $bare = ($text -replace '^const\s+', '') -replace '\*', ' '
        $words = @($bare -split '\s+' | Where-Object { $_ -ne '' })
        if ($words.Count -ne 2) { throw "Cannot read parameter '$text' of $($Entry.Name)" }
        $result += [pscustomobject]@{
            Name = $words[1]; Type = $words[0]; Pointer = $pointer; Const = $const
        }
    }
    return ,$result
}

# A compiled slot's pointer parameter: the C expression of its byte count.
function Get-V9xGlPointerBytes {
    param($Entry, $Param, $Rules)

    $name = [string]$Entry.Name
    if ($Rules.Bytes.ContainsKey($name)) {
        return [string]$Rules.Bytes[$name]
    }
    $size = "(v9x_u32)sizeof($($Param.Type))"
    if ($name -match '^gl(Color|Normal|RasterPos|TexCoord|Vertex|EvalCoord)(\d)(b|d|f|i|s|ub|ui|us)v$') {
        return "$($Matches[2])u * $size"
    }
    if ($name -match '^glRect(d|f|i|s)v$') {
        return "2u * $size"
    }
    if ($name -match '^glIndex(d|f|i|s)v$') {
        return $size
    }
    throw "gl_dlist_rules.psd1 has no Bytes rule for $name's pointer $($Param.Name)."
}

function Get-V9xGlListKind {
    param($Entry, $Rules)

    $name = [string]$Entry.Name
    if ($name.StartsWith('glGet') -or @($Rules.Execute) -contains $name) {
        return 'execute'
    }
    if (@($Rules.Special) -contains $name) { return 'special' }
    if (@($Rules.Deferred) -contains $name) { return 'deferred' }
    return 'compiled'
}

function Write-V9xGlDispatchHeader {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$OutputDir
    )

    $manifestPath = Join-Path $RepoRoot "src\opengl\gl_entrypoints.psd1"
    $manifest = Import-PowerShellDataFile -LiteralPath $manifestPath
    $entries = @($manifest.Entries)
    if ($entries.Count -ne 336) {
        throw "gl_entrypoints.psd1 has $($entries.Count) entries; the 1.1 table has 336."
    }
    for ($i = 0; $i -lt $entries.Count; $i++) {
        if ([int]$entries[$i].Slot -ne $i) {
            throw "gl_entrypoints.psd1 entry $i has Slot $($entries[$i].Slot)."
        }
    }
    $names = @($entries | ForEach-Object { $_.Name })
    if (($names | Sort-Object -Unique).Count -ne $names.Count) {
        throw "gl_entrypoints.psd1 has a duplicate name."
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("/*")
    $lines.Add(" * Generated by scripts\lib\gl-dispatch.ps1 from")
    $lines.Add(" * src\opengl\gl_entrypoints.psd1. Do not edit: rerun the build.")
    $lines.Add(" */")
    $lines.Add("#ifndef VELOCITY9X_GL_DISPATCH_GEN_H")
    $lines.Add("#define VELOCITY9X_GL_DISPATCH_GEN_H")
    $lines.Add("")
    $lines.Add('#include "velocity9x/gl_types.h"')
    $lines.Add("")
    $lines.Add("#ifndef V9X_GL_API")
    $lines.Add("#error V9X_GL_API must name the slot calling convention before this header")
    $lines.Add("#endif")
    $lines.Add("")
    $lines.Add("#define V9X_GL_SLOT_COUNT $($entries.Count)")
    $lines.Add("typedef void (V9X_GL_API *V9X_GL_PROC)(void);")
    $lines.Add("")
    $lines.Add("#define V9X_GL_SLOT_NAMES { \")
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $sep = if ($i -lt $entries.Count - 1) { "," } else { "" }
        $lines.Add("    `"$($entries[$i].Name)`"$sep \")
    }
    $lines.Add("}")
    $lines.Add("")
    foreach ($e in $entries) {
        $lines.Add("typedef $($e.Return) (V9X_GL_API *V9X_GL_PFN_$($e.Name))($($e.Params));")
    }
    $lines.Add("")
    # Each entry's parameter names, in order.
    $argNames = @{}
    foreach ($e in $entries) {
        $params = [string]$e.Params
        $found = @()
        if ($params -ne 'void') {
            foreach ($p in ($params -split ',')) {
                $token = ($p.Trim() -replace '\[.*\]$', '')
                $name = ($token -split '\s+')[-1].TrimStart('*')
                if ($name -eq '') { throw "Cannot name a parameter of $($e.Name): '$p'" }
                $found += $name
            }
        }
        $argNames[$e.Name] = $found
    }
    $lines.Add("#ifdef V9X_GL_DEFINE_STUBS")
    foreach ($e in $entries) {
        $ret = [string]$e.Return
        $params = [string]$e.Params
        $body = @()
        foreach ($name in $argNames[$e.Name]) {
            $body += "(void)$name;"
        }
        $body += "V9X_GL_STUB_HOOK($($e.Slot));"
        if ($ret -ne 'void') {
            $body += "return ($ret)0;"
        }
        $lines.Add("static $ret V9X_GL_API v9x_gl_stub_$($e.Name)($params) { $($body -join ' ') }")
    }
    $lines.Add("#endif /* V9X_GL_DEFINE_STUBS */")
    $lines.Add("")
    # Display lists: each slot's kind, its save function and replay case.
    $rulesPath = Join-Path $RepoRoot "src\opengl\gl_dlist_rules.psd1"
    $rules = Import-PowerShellDataFile -LiteralPath $rulesPath
    foreach ($listed in @($rules.Execute) + @($rules.Special) + @($rules.Deferred) + @($rules.Bytes.Keys)) {
        if ($names -notcontains $listed) { throw "gl_dlist_rules.psd1 names $listed, which is not a slot." }
    }
    $kinds = @{}
    foreach ($e in $entries) {
        $kinds[$e.Name] = Get-V9xGlListKind -Entry $e -Rules $rules
        if ($kinds[$e.Name] -eq 'compiled' -and [string]$e.Return -ne 'void') {
            throw "$($e.Name) returns a value and cannot be compiled into a list."
        }
    }
    $lines.Add("#if defined(V9X_GL_DEFINE_CENSUS) && defined(V9X_GL_DEFINE_SAVE)")
    foreach ($e in $entries) {
        if ($kinds[$e.Name] -eq 'special') {
            $lines.Add("static int v9x_gl_save_$($e.Name)($($e.Params));")
        }
    }
    foreach ($e in $entries) {
        if ($kinds[$e.Name] -ne 'compiled') { continue }
        $params = Get-V9xGlParams -Entry $e
        $decl = @('v9x_u32 *w_;')
        $fixedWords = 0
        $payload = @()
        $k = 0
        foreach ($p in $params) {
            if ($p.Pointer) {
                if (-not $p.Const) { throw "$($e.Name) writes through $($p.Name) and cannot be compiled." }
                $bytes = Get-V9xGlPointerBytes -Entry $e -Param $p -Rules $rules
                $decl += "v9x_u32 b$($k)_ = $($p.Name) != 0 ? ($bytes) : 0u;"
                $payload += "((b$($k)_ + 3u) >> 2)"
                $fixedWords += 1
                $k++
            } elseif ($p.Type -eq 'GLdouble' -or $p.Type -eq 'GLclampd') {
                $fixedWords += 2
            } elseif ($p.Type -eq 'GLfloat' -or $p.Type -eq 'GLclampf') {
                $fixedWords += 1
            } elseif ($script:V9xGlIntegerTypes -contains $p.Type) {
                $fixedWords += 1
            } else {
                throw "$($e.Name): no list rule for type $($p.Type)."
            }
        }
        $body = @()
        if ($params.Count -eq 0) {
            $body += "(void)V9X_GL_DLIST_NODE($($e.Slot)u, 0u);"
        } else {
            $decl += 'v9x_u32 i_ = 0u;'
            $total = (@("$($fixedWords)u") + $payload) -join ' + '
            $body += "w_ = V9X_GL_DLIST_NODE($($e.Slot)u, $total);"
            $body += "if (w_ != 0) {"
            $k = 0
            foreach ($p in $params) {
                if ($p.Pointer) {
                    $body += "    w_[i_++] = $($p.Name) != 0 ? b$($k)_ : V9X_GL_DLIST_NULL;"
                    $body += "    if ($($p.Name) != 0) { V9X_GL_DLIST_COPY(&w_[i_], $($p.Name), b$($k)_); }"
                    $body += "    i_ += (b$($k)_ + 3u) >> 2;"
                    $k++
                } elseif ($p.Type -eq 'GLdouble' -or $p.Type -eq 'GLclampd') {
                    $body += "    V9X_GL_DLIST_COPY(&w_[i_], &$($p.Name), 8u); i_ += 2u;"
                } elseif ($p.Type -eq 'GLfloat' -or $p.Type -eq 'GLclampf') {
                    $body += "    V9X_GL_DLIST_COPY(&w_[i_], &$($p.Name), 4u); i_ += 1u;"
                } else {
                    $body += "    w_[i_++] = (v9x_u32)$($p.Name);"
                }
            }
            $body += "    (void)i_;"
            $body += "}"
        }
        if ($params.Count -eq 0) { $decl = @() }
        $lines.Add("static int v9x_gl_save_$($e.Name)($($e.Params))")
        $lines.Add("{")
        foreach ($d in $decl) { $lines.Add("    $d") }
        foreach ($b in $body) { $lines.Add("    $b") }
        $lines.Add("    return V9X_GL_DLIST_COMPILE_ONLY();")
        $lines.Add("}")
    }
    # Replay: a node's slot called with the arguments it holds.
    $lines.Add("static void v9x_gl_dlist_replay_node(v9x_u32 slot, const v9x_u32 *w, v9x_u32 words)")
    $lines.Add("{")
    $lines.Add("    switch (slot) {")
    foreach ($e in $entries) {
        $kind = $kinds[$e.Name]
        if ($kind -eq 'special') {
            $lines.Add("    case $($e.Slot)u: V9X_GL_REPLAY_SPECIAL($($e.Slot)u, w, words); break;")
            continue
        }
        if ($kind -ne 'compiled') { continue }
        $params = Get-V9xGlParams -Entry $e
        $lines.Add("    case $($e.Slot)u: {")
        $i = 0
        foreach ($p in $params) {
            if ($p.Pointer) {
                $lines.Add("        const $($p.Type) *a$($i)_;")
            } else {
                $lines.Add("        $($p.Type) a$($i)_;")
            }
            $i++
        }
        if ($params.Count -gt 0) { $lines.Add("        v9x_u32 i_ = 0u;") }
        $i = 0
        foreach ($p in $params) {
            if ($p.Pointer) {
                $lines.Add("        if (w[i_] == V9X_GL_DLIST_NULL) { a$($i)_ = 0; i_ += 1u; }")
                $lines.Add("        else { a$($i)_ = (const $($p.Type) *)(const void *)&w[i_ + 1u]; i_ += 1u + ((w[i_] + 3u) >> 2); }")
            } elseif ($p.Type -eq 'GLdouble' -or $p.Type -eq 'GLclampd') {
                $lines.Add("        V9X_GL_DLIST_COPY(&a$($i)_, &w[i_], 8u); i_ += 2u;")
            } elseif ($p.Type -eq 'GLfloat' -or $p.Type -eq 'GLclampf') {
                $lines.Add("        V9X_GL_DLIST_COPY(&a$($i)_, &w[i_], 4u); i_ += 1u;")
            } else {
                $lines.Add("        a$($i)_ = ($($p.Type))w[i_]; i_ += 1u;")
            }
            $i++
        }
        if ($params.Count -gt 0) { $lines.Add("        (void)i_;") }
        $argList = @()
        for ($j = 0; $j -lt $params.Count; $j++) { $argList += "a$($j)_" }
        $lines.Add("        ((V9X_GL_PFN_$($e.Name))V9X_GL_REPLAY_TARGET($($e.Slot)u))($($argList -join ', '));")
        $lines.Add("        break;")
        $lines.Add("    }")
    }
    $lines.Add("    default:")
    $lines.Add("        break;")
    $lines.Add("    }")
    $lines.Add("    (void)words;")
    $lines.Add("}")
    $lines.Add("#endif /* V9X_GL_DEFINE_CENSUS && V9X_GL_DEFINE_SAVE */")
    $lines.Add("")

    $lines.Add("#ifdef V9X_GL_DEFINE_CENSUS")
    foreach ($e in $entries) {
        $ret = [string]$e.Return
        $call = "((V9X_GL_PFN_$($e.Name))V9X_GL_CENSUS_TARGET($($e.Slot)))($($argNames[$e.Name] -join ', '))"
        $tail = if ($ret -eq 'void') { "$call;" } else { "return $call;" }
        $kind = $kinds[$e.Name]
        if ($kind -eq 'compiled' -or $kind -eq 'special') {
            $lines.Add("static $ret V9X_GL_API v9x_gl_census_$($e.Name)($($e.Params)) { V9X_GL_CENSUS_HOOK($($e.Slot));")
            $lines.Add("#ifdef V9X_GL_DEFINE_SAVE")
            $lines.Add("    if (V9X_GL_SAVING() && v9x_gl_save_$($e.Name)($($argNames[$e.Name] -join ', '))) { return; }")
            $lines.Add("#endif")
            $lines.Add("    $tail }")
        } elseif ($kind -eq 'deferred') {
            $lines.Add("static $ret V9X_GL_API v9x_gl_census_$($e.Name)($($e.Params)) { V9X_GL_CENSUS_HOOK($($e.Slot));")
            $lines.Add("#ifdef V9X_GL_DEFINE_SAVE")
            $lines.Add("    if (V9X_GL_SAVING()) { V9X_GL_DLIST_UNCOMPILED($($e.Slot)u); }")
            $lines.Add("#endif")
            $lines.Add("    $tail }")
        } else {
            $lines.Add("static $ret V9X_GL_API v9x_gl_census_$($e.Name)($($e.Params)) { V9X_GL_CENSUS_HOOK($($e.Slot)); $tail }")
        }
    }
    $lines.Add("")
    $lines.Add("#define V9X_GL_CENSUS_INIT { \")
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $sep = if ($i -lt $entries.Count - 1) { "," } else { "" }
        $lines.Add("    (V9X_GL_PROC)v9x_gl_census_$($entries[$i].Name)$sep \")
    }
    $lines.Add("}")
    $lines.Add("#endif /* V9X_GL_DEFINE_CENSUS */")
    $lines.Add("")
    $lines.Add("#define V9X_GL_DISPATCH_INIT { \")
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $sep = if ($i -lt $entries.Count - 1) { "," } else { "" }
        $lines.Add("    (V9X_GL_PROC)v9x_gl_stub_$($entries[$i].Name)$sep \")
    }
    $lines.Add("}")
    $lines.Add("")
    $lines.Add("#endif /* VELOCITY9X_GL_DISPATCH_GEN_H */")

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $target = Join-Path $OutputDir "gl_dispatch_gen.h"
    [System.IO.File]::WriteAllLines($target, $lines, (New-Object System.Text.UTF8Encoding($false)))
    return $target
}
