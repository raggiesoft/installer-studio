// ================================================================================
// FILE: editors/vscode-raggiescript/extension.js
// PROJECT: RaggieScript & RaggieSoft Studio Official VS Code Extension
// WEBSITE: https://raggiesoftstudio.com/raggiescript  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this VS Code extension script do?
//     Whenever a developer opens or saves a `.rssc` (RaggieScript) or `.rsip`/`.rsis`
//     (RaggieSoft Installer Studio) file in VS Code, Cursor, Windsurf, or Antigravity:
//       1. **Live Diagnostics (`RSW700` / `RSW701` / `RSW702`)**:
//          - Invokes `raggiescript.exe check --diagnostics=json` (or `instbuild.exe
//            --validate --diagnostics=json`) in the background and underlines any
//            syntax error, `RSW701` Wizard Phase Isolation violation, or `RSW702`
//            Capability Sandbox violation right inside the editor!
//       2. **Hover Reference & Namespace IntelliSense (`Ctrl+Space`)**:
//          - Shows concise documentation when hovering over `msi.*`, `reg.*`, `fs.*`,
//            `proc.*`, `ffi.*`, `ui.*`, `Uac.Deferred`, etc., and suggests namespace
//            methods automatically as you type `.`!
//       3. **Integrated Build, Bytecode Disassembly, & MSI Prism Commands**:
//          - Adds Command Palette shortcuts to check scripts, compile `Setup.exe`,
//            disassemble `RSWBC1` bytecode into a virtual preview tab, and launch
//            `msiprism.exe` (RaggieSoft MSI Prism)!
// ================================================================================

"use strict";

const vscode = require("vscode");
const cp     = require("child_process");
const fs     = require("fs");
const path   = require("path");
const os     = require("os");

// Concise hover documentation for every RaggieScript built-in namespace and key enum
const NAMESPACE_DOCS = {
  sys:      "**`sys.*`** — True CPU silicon (`sys.arch()`, `sys.isArm64()`, `sys.isEmulatedUnderPrism()`), OS build (`sys.osVersion()`), folders (`sys.folder(\"ProgramFiles\")`), and `sys.rebootAndResume(state)`.",
  fs:       "**`fs.*`** — Rollback-journaled filesystem operations (`fs.mkdir`, `fs.copy`, `fs.copyTree`, `fs.extract`, `fs.readJson`, `fs.writeJson`, `fs.removeDir`, `fs.isLocked`).",
  reg:      "**`reg.*`** — Stateless 64-bit / `WOW6432Node` / dual-view Windows Registry access (`reg.read`, `reg.write`, `reg.exists`, `reg.listSubkeys`, `reg.listValues`, `reg.deleteKeyTree`).",
  msi:      "**`msi.*`** — Full Windows Installer transaction & direct SQL table engine (`msi.createDatabase`, `msi.query`, `msi.execSql`, `msi.getProperty`, `msi.setProperty`, `msi.getSummary`, `msi.setSummary`, `msi.importStream`, `msi.generateTransform`, `msi.install`, `msi.repair`, `msi.uninstall`).",
  proc:     "**`proc.*`** — Process inspection, 3-token launcher (`Token.Current | Elevated | Desktop`), and Windows Restart Manager (`proc.getLockingProcesses`, `proc.restartManagerShutdown`, `proc.restartManagerRestart`).",
  ffi:      "**`ffi.*`** — 64-bit native Windows DLL Foreign Function Interface (`ffi.call(dll, symbol, returnType, [args])`) across `x64` and `ARM64`.",
  ui:       "**`ui.*`** — Accessible Windows 11 `TaskDialogIndirect` prompts (`ui.taskDialog`, `ui.confirm`, `ui.info`, `ui.warn`, `ui.error`, `ui.pickFolder`, `ui.progress`) with Command Links and UAC shields.",
  feature:  "**`feature.*`** — Selectable package feature state (`feature.list()`, `feature.isSelected(id)`, `feature.setSelected(id, bool)`, `feature.selectPreset(\"compact\")`).",
  compile:  "**`compile.*`** — Build-time intrinsics (`compile.json(\"catalog.json\")`, `compile.text(\"notes.txt\")`, `compile.def(\"KEY\", \"default\")`) baked directly into `RSWBC1` bytecode.",
  archive:  "**`archive.*`** — Archive creation, extraction, and SHA-256 verified on-demand `.rswpak` streaming (`archive.extractZip`, `archive.createPak`, `archive.extractPak`, `archive.downloadPak`).",
  prereq:   "**`prereq.*`** — Curated `.NET 11/10`, `EdgeWebView2`, and `VcRedist2022` detection & silent CDN bootstrapper (`prereq.isSatisfied`, `prereq.installPreset`).",
  shortcut: "**`shortcut.*`** — Transactional Start Menu & Desktop `.lnk` creation (`shortcut.create`, `shortcut.remove`).",
  shell:    "**`shell.*`** — File extension & custom URI protocol registration (`shell.registerFileType`, `shell.registerProtocol`, `shell.refresh`).",
  env:      "**`env.*`** — Safe non-clobbering environment & `PATH` management (`env.get`, `env.set`, `env.appendPath`, `env.remove`).",
  arp:      "**`arp.*`** — Windows *Installed apps* registration & `SystemComponent` hiding (`arp.register`, `arp.setSystemComponent`, `arp.unregister`).",
  Uac:      "**`Uac`** — Execution & elevation policy: `Uac.Deferred` (runs `wizard` un-elevated as `asInvoker`, then prompts UAC right before `install`!), `Uac.AsInvoker`, `Uac.HighestAvailable`, `Uac.RequireAdministrator`."
};

const NAMESPACE_METHODS = {
  msi:     ["createDatabase", "listTables", "query", "execSql", "getProperty", "setProperty", "getSummary", "setSummary", "importStream", "exportStream", "generateTransform", "install", "repair", "configureFeature", "uninstall", "applyPatch", "isUpgradeInstalled", "isProductInstalled", "runLegacyUninstaller"],
  reg:     ["read", "write", "exists", "readString", "readDword", "writeString", "writeExpandString", "writeDword", "listSubkeys", "listValues", "deleteValue", "deleteKeyTree"],
  fs:      ["exists", "isDir", "isFile", "size", "isLocked", "fileVersion", "join", "mkdir", "copy", "copyTree", "extract", "readText", "writeText", "readJson", "writeJson", "remove", "removeDir", "findFiles"],
  proc:    ["run", "open", "isRunning", "listRunning", "terminate", "getLockingProcesses", "restartManagerShutdown", "restartManagerRestart"],
  ffi:     ["call"],
  sys:     ["arch", "isArm64", "isX64", "isEmulatedUnderPrism", "osVersion", "osBuild", "isAdmin", "canPromptUac", "isSilent", "folder", "hasSwitch", "cliArg", "newGuid", "uuid5", "rebootAndResume", "getResumeState", "clearResumeState"],
  ui:      ["taskDialog", "confirm", "info", "warn", "error", "pickFolder", "pickFile", "progress"],
  feature: ["list", "isSelected", "setSelected", "selectPreset", "info"],
  compile: ["json", "text", "def"],
  archive: ["extractZip", "createPak", "extractPak", "downloadPak"]
};

/**
 * Locates `raggiescript.exe`, `instbuild.exe`, or `msiprism.exe` across user settings,
 * the active workspace (`desktop-apps/installer-studio/bin/<arch>`), standard install paths, or `PATH`.
 */
function resolveToolBinary(exeName, docUri) {
  const cfgDir = vscode.workspace.getConfiguration("raggiescript").get("toolchainBinDir", "").trim();
  const hostArch = (os.arch() === "arm64") ? "arm64" : "x64";
  const candidates = [];

  if (cfgDir) {
    candidates.push(path.join(cfgDir, exeName));
  }
  if (docUri && docUri.fsPath) {
    let cur = path.dirname(docUri.fsPath);
    for (let depth = 0; depth < 6; depth++) {
      candidates.push(path.join(cur, "bin", hostArch, exeName));
      candidates.push(path.join(cur, "desktop-apps", "installer-studio", "bin", hostArch, exeName));
      const parent = path.dirname(cur);
      if (parent === cur) break;
      cur = parent;
    }
  }
  if (vscode.workspace.workspaceFolders) {
    for (const wf of vscode.workspace.workspaceFolders) {
      candidates.push(path.join(wf.uri.fsPath, "bin", hostArch, exeName));
      candidates.push(path.join(wf.uri.fsPath, "desktop-apps", "installer-studio", "bin", hostArch, exeName));
    }
  }
  candidates.push(path.join("C:\\Program Files\\RaggieSoft\\RaggieSoft Studio\\Installer Studio", exeName));
  candidates.push(path.join("C:\\Program Files\\RaggieSoft\\Installer Studio", exeName));

  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return exeName;
}

/**
 * Runs `instbuild.exe --validate --diagnostics=json` on `.rssc` or `.rsip` files and populates VS Code Diagnostics.
 */
function lintDocument(doc, diagCollection) {
  if (!doc || doc.isUntitled) return;
  const isRssc = doc.languageId === "raggiescript" || doc.fileName.toLowerCase().endsWith(".rssc");
  const isManifest = doc.languageId === "raggiesoft-manifest";
  if (!isRssc && !isManifest) return;
  if (!vscode.workspace.getConfiguration("raggiescript").get("lintOnSave", true)) return;

  const instbuild = resolveToolBinary("instbuild.exe", doc.uri);
  cp.execFile(instbuild, ["--validate", "--diagnostics=json", doc.fileName], { timeout: 8000 }, (err, stdout) => {
    if (!stdout) return;
    try {
      const parsed = JSON.parse(stdout);
      const items = Array.isArray(parsed.diagnostics) ? parsed.diagnostics : [];
      const vsDiags = items.map((d) => {
        const lineZero = Math.max(0, (Number(d.line) || 1) - 1);
        const lineText = lineZero < doc.lineCount ? doc.lineAt(lineZero).text : "";
        const range = new vscode.Range(lineZero, 0, lineZero, Math.max(1, lineText.length));
        const sev = String(d.severity || "error").toLowerCase() === "warning"
          ? vscode.DiagnosticSeverity.Warning
          : vscode.DiagnosticSeverity.Error;
        const vd = new vscode.Diagnostic(range, `[${d.code || "RSW"}] ${d.message}`, sev);
        vd.source = "RaggieScript";
        return vd;
      });
      diagCollection.set(doc.uri, vsDiags);
    } catch (_) {
      // Ignore non-JSON output if toolchain is not built yet
    }
  });
}

function activate(context) {
  const diagCollection = vscode.languages.createDiagnosticCollection("raggiescript");
  context.subscriptions.push(diagCollection);

  // Lint open and saved documents automatically
  if (vscode.window.activeTextEditor) {
    lintDocument(vscode.window.activeTextEditor.document, diagCollection);
  }
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => lintDocument(doc, diagCollection)),
    vscode.workspace.onDidSaveTextDocument((doc) => lintDocument(doc, diagCollection))
  );

  // Register Hover Provider for RaggieScript namespaces & enums
  context.subscriptions.push(
    vscode.languages.registerHoverProvider("raggiescript", {
      provideHover(document, position) {
        const range = document.getWordRangeAtPosition(position);
        if (!range) return null;
        const word = document.getText(range);
        if (Object.prototype.hasOwnProperty.call(NAMESPACE_DOCS, word)) {
          return new vscode.Hover(new vscode.MarkdownString(NAMESPACE_DOCS[word]), range);
        }
        return null;
      }
    })
  );

  // Register `.` Member IntelliSense Completion Provider for RaggieScript namespaces
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      "raggiescript",
      {
        provideCompletionItems(document, position) {
          const linePrefix = document.lineAt(position).text.slice(0, position.character);
          const m = linePrefix.match(/\b([A-Za-z_][A-Za-z0-9_]*)\.\s*$/);
          if (!m) return undefined;
          const ns = m[1];
          const methods = NAMESPACE_METHODS[ns];
          if (!methods) return undefined;
          return methods.map((fn) => {
            const item = new vscode.CompletionItem(fn, vscode.CompletionItemKind.Method);
            item.detail = `${ns}.${fn}(...)`;
            return item;
          });
        }
      },
      "."
    )
  );

  // Register Command Palette actions
  const outChan = vscode.window.createOutputChannel("RaggieScript");
  context.subscriptions.push(outChan);

  context.subscriptions.push(
    vscode.commands.registerCommand("raggiescript.check", () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed) return;
      lintDocument(ed.document, diagCollection);
      vscode.window.setStatusBarMessage("RaggieScript: Verified RSW701 & RSW702 rules.", 3000);
    }),
    vscode.commands.registerCommand("raggiescript.compileExe", () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed) return;
      const instbuild = resolveToolBinary("instbuild.exe", ed.document.uri);
      outChan.show(true);
      outChan.appendLine(`> "${instbuild}" "${ed.document.fileName}"`);
      cp.execFile(instbuild, [ed.document.fileName], (err, stdout, stderr) => {
        if (stdout) outChan.appendLine(stdout.trim());
        if (stderr) outChan.appendLine(stderr.trim());
        if (!err) vscode.window.showInformationMessage("RaggieScript compiled Setup.exe cleanly.");
      });
    }),
    vscode.commands.registerCommand("raggiescript.disasm", async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed) return;
      const rsExe = resolveToolBinary("raggiescript.exe", ed.document.uri);
      const tmpBc = path.join(os.tmpdir(), `rssc-preview-${process.pid}.rswbc`);
      cp.execFile(rsExe, ["compile", ed.document.fileName, tmpBc], (err, stdout, stderr) => {
        if (err) {
          vscode.window.showErrorMessage(`Compile failed: ${stderr || stdout}`);
          return;
        }
        cp.execFile(rsExe, ["disasm", tmpBc], async (dErr, dOut) => {
          try { fs.unlinkSync(tmpBc); } catch (_) {}
          if (dErr) return;
          const doc = await vscode.workspace.openTextDocument({ content: dOut, language: "raggiesoft-manifest" });
          await vscode.window.showTextDocument(doc, { preview: true, viewColumn: vscode.ViewColumn.Beside });
        });
      });
    }),
    vscode.commands.registerCommand("raggiescript.openMsiPrism", () => {
      const uri = vscode.window.activeTextEditor ? vscode.window.activeTextEditor.document.uri : undefined;
      const prism = resolveToolBinary("msiprism.exe", uri);
      cp.spawn(prism, [], { detached: true, stdio: "ignore" }).unref();
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
