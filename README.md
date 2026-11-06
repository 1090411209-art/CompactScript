# CompactScript
CompactScript is a personally created programming language. As its name suggests, its syntax is very compact. You are free to explore the various code within it.

You can use the help command internally to output command references and introductory articles; hopefully this helps you.
***
You can choose either the HTML version or the C++ version.

**If you need the .exe installer, please go to the Releases page.**
***
# All implemented commands
## System & Utility

| Command | Purpose | Syntax |
|---|---|---|
| `clear` | Clear the screen | `clear` |
| `help` | Show help | `help` |
| `save` | Export `.cscript` file | `save"name"` / `save"name"-xor` |
| `load` | Open file picker to import | `load` |
| `run` | Execute multiple commands | `run(cmd1)-(cmd2)-...` |
| `time` | Delay, then run a command | `time"N"-command` / `time["var"]-command` |
| `repeat` | Repeat a command N times | `repeat"N"-command` / `repeat["var"]-command` |
| `random` | Random pick / shuffle / multi-run | `random(c1)-(c2)` / `random[shuffle]-...` / `random"N"-...` |
| `random-number` | Random integer in range | `random-number"1"-"10"` |
| `random-text` | Random pick one text | `random-text"a"-"b"` |
| `random-uuid` | Generate a UUID | `random-uuid` |

## Variables

| Command | Purpose | Syntax |
|---|---|---|
| `text` | Create / output text variable | `text["n"]"v"` / `text["n"]` / `text[concatenation]-"expr"` |
| `number` | Create / output number variable | `number["n"]"123"` / `number["n"]` / `number[concatenation]-"expr"` |
| `text-color` | Colored text output | `text-color["red"]"text"` |
| `number-color` | Colored number output | `number-color["red"]"123"` |
| `variable` | List all variables | `variable` |
| `variable-delete` | Delete variable(s) | `variable-delete "n"` / `variable-delete =all` |
| `variable-rename` | Rename a variable | `variable-rename["old"]"new"` |
| `variable-replace` | Replace variable content | `variable-replace["n"]"newValue"` |
| `variable-type` | Convert type | `variable-type["n"]-text` / `-number` |
| `variable-color` | Set variable color | `variable-color["n"]-color["red"]` |
| `variable-exist` | Branch on variable existence | `variable-exist["n"]-true=(cmd)-false=(cmd)` |

## Lists

| Command | Purpose | Syntax |
|---|---|---|
| `list` | Create / list / execute item | `list["n"]-"i"(cmd)-...` / `list["n"]"i"` / `list` |
| `list-add` | Add an item | `list-add["n"]-"item"(cmd)` |
| `list-delete` | Delete item / clear list / all lists | `list-delete["n"]"item"` / `list-delete["n"]=all` / `list-delete =all` |
| `list-rename` | Rename a list | `list-rename["old"]"new"` |
| `list-replace` | Replace item name + command | `list-replace["n"]"old"-"new"(cmd)` |
| `list-exist` | Branch on list / item existence | `list-exist["n"]-"item"-true=(cmd)-false=(cmd)` |
| `length` | Length of string / list / function | `length"text"` / `length[list]-["n"]` / `length[functional]-["n"]` |

## Functions

| Command | Purpose | Syntax |
|---|---|---|
| `functional` | Define / call / list functions | `functional["n"]-(c1)-(c2)` / `functional["n"]` / `functional` |
| `functional-rename` | Rename a function | `functional-rename["old"]"new"` |
| `functional-delete` | Delete function(s) | `functional-delete "n"` / `functional-delete =all` |
| `functional-replace` | Replace function body | `functional-replace["n"]-(c1)-(c2)-...` |
| `functional-exist` | Branch on function existence | `functional-exist["n"]-true=(cmd)-false=(cmd)` |

## Flow Control

| Command | Purpose | Syntax |
|---|---|---|
| `if` | Conditional branch | `if["a"]-eq-n"1"-true=(cmd)-false=(cmd)` |
| `while` | Loop while condition holds | `while["a"]-lt-n"10"-do(cmd1)-(cmd2)` |

## Wait

| Command | Purpose | Syntax |
|---|---|---|
| `wait` | Wait until variable matches, then run | `wait["a"]="v"-(cmd)` / `wait[appear]-["a"]-(cmd)` |
| `wait-time` | Wait with extra delay | `wait-time"N"-["a"]="v"-(cmd)` |
| `wait-cancel` | Cancel pending waits | `wait-cancel=all` / `=numbered` / `=unnumbered` / `"id"` |

## Input

| Command | Purpose | Syntax |
|---|---|---|
| `input` | Prompt user into variable | `input["n"]-text"prompt"` / `input["n"]-number"prompt"` |
| `input-choose` | Choice input / long-press input | `input-choose["n"]-text"p"-"opt"="val"` / `input-choose[long-press]-...` |

## Computation

| Command | Purpose | Syntax |
|---|---|---|
| `computation` | Evaluate / modulo / round / store | `computation"1"+"1"` / `computation[modulo]"12"/"5"` / `computation[round]"7.2"` / `computation[variable]"1"+"1"-["v"]` |

## Notes

- Operators: `+ - * / ^ ** sqrt() n!`; comparisons: `eq ne gt lt ge le`
- Operands: `["var"]`, `-n"123"`, `-t"text"`
- Colors (24): `red light_red dark_red`, `green ...`, `yellow ...`, `blue ...`, `cyan ...`, `magenta ...`, `orange ...`, `purple ...`
- Format tokens: `-n-` (newline), `-nr-` (newline + reset), `-r-` (reset)
- Exist branches: `-true=(cmd)` / `-false=(cmd)`; use `(null)` to skip; not both.
