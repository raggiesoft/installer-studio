" ================================================================================
" FILE: editors/vim/syntax/raggiescript.vim
" PROJECT: RaggieScript Syntax Highlighting for Vim & Neovim
" ================================================================================
if exists("b:current_syntax")
  finish
endif

syn keyword rsscKeyword use include import package showcase app feature param payload wizard step when install repair uninstall on fn function let const ref if elif else for in while repeat until break continue return try catch throw exit from
syn keyword rsscNamespace sys fs reg ini xml json str regex archive crypto net proc ffi msi prereq shortcut shell env arp svc task firewall acl ui compile showcase log product def assert coalesce len range typeof sleep
syn keyword rsscType bool int float string path version array map any Scope Uac Token RegView Icon Hive SetupMode WizardStyle Delivery
syn keyword rsscConstant true false null EXIT_OK EXIT_ELEVATION_REQUIRED EXIT_REBOOT_NEEDED EXIT_UNSUPPORTED_OS EXIT_PREREQ_MISSING EXIT_DOWNLOAD_FAILED

syn match rsscPreProc "^\s*#\(if\|ifdef\|ifndef\|elif\|else\|endif\|define\|error\|region\|endregion\|include\)\>.*$"
syn match rsscComment "//.*$"
syn region rsscBlockComment start="/\*" end="\*/"
syn match rsscVersion '\<[vV]"[0-9.]\+"'
syn region rsscRawString start='\<[rR]"' end='"'
syn region rsscString start='"' skip='\\"' end='"' contains=rsscInterp
syn region rsscInterp start='\${' end='}' contained
syn match rsscNumber "\<\(0[xX][0-9A-Fa-f_]\+\|[0-9][0-9_.]*\)\>"

hi def link rsscKeyword Keyword
hi def link rsscNamespace Function
hi def link rsscType Type
hi def link rsscConstant Boolean
hi def link rsscPreProc PreProc
hi def link rsscComment Comment
hi def link rsscBlockComment Comment
hi def link rsscVersion Special
hi def link rsscRawString String
hi def link rsscString String
hi def link rsscInterp Special
hi def link rsscNumber Number

let b:current_syntax = "raggiescript"
