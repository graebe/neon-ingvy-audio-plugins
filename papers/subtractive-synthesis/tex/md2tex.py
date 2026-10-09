# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Torben Gräber
"""
paper.md -> paper.tex: the same paper, typeset as a two-column conference
paper (IEEEtran, conference mode).

The Markdown is the one source. This changes the FORM and nothing else: every
sentence, equation, listing, table and reference is carried over as written,
so the two cannot drift -- regenerate rather than edit paper.tex.

What it decides, being layout:
  - a table goes across both columns when it has five or more columns or long
    rows; Tables 1-3 (a "**Table n.**" line before them) become captioned floats
  - a figure goes across both columns, scaled to its number of panels
  - a display equation too wide for a column is broken at its top-level
    \\quad / \\qquad separators into stacked lines
  - section, subsection and equation numbers are the Markdown's own, set
    explicitly, so "section 4.3" and "Eq. (11)" in the text stay true
  - citations [n] / [Sn] become \\cite, and the reference list a bibliography
    with the same numbers

  python3 md2tex.py ../paper.md paper.tex
"""

import re
import sys
from pathlib import Path

# ------------------------------------------------------------------ inline


def esc(s):
    """Escapes TeX's special characters in plain text."""
    out = []
    for c in s:
        out.append({
            '\\': r'\textbackslash{}', '&': r'\&', '%': r'\%', '#': r'\#',
            '_': r'\_', '{': r'\{', '}': r'\}', '~': r'\textasciitilde{}',
            '^': r'\textasciicircum{}', '$': r'\$',
        }.get(c, c))
    return ''.join(out)


def tt(code):
    """Inline code: monospace, breakable after path and module separators."""
    s = esc(code)
    for sep in ('::', '/', r'\_', '.', r'\#'):
        s = s.replace(sep, sep + r'\allowbreak{}')
    return r'\texttt{' + s + '}'


def cite_keys(body):
    return ','.join(k if k.startswith('S') else f'r{k}' for k in re.split(r',\s*', body))


def inline(s):
    """One run of Markdown text to TeX."""
    keep = []

    def hold(tex):
        keep.append(tex)
        return f'\x00{len(keep) - 1}\x00'

    # Links first: their text is converted on its own, code spans included.
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)',
               lambda m: hold(rf'\href{{{m[2]}}}{{{inline(m[1])}}}' if m[2].startswith('http') else inline(m[1])), s)
    s = re.sub(r'`([^`]+)`', lambda m: hold(tt(m[1])), s)
    s = re.sub(r'(?<!\\)\$([^$]+?)\$', lambda m: hold(f'${m[1]}$'), s)
    s = re.sub(r'<(https?://[^>]+)>', lambda m: hold(rf'\url{{{m[1]}}}'), s)
    s = re.sub(r'\[((?:\d+|S\d)(?:,\s*(?:\d+|S\d))*)\]', lambda m: hold(rf'\cite{{{cite_keys(m[1])}}}'), s)
    s = esc(s)
    s = re.sub(r'\*\*(.+?)\*\*', r'\\textbf{\1}', s)
    s = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'\\emph{\1}', s)
    s = re.sub(r'"([^"\n]+)"', r"``\1''", s)
    while '\x00' in s:
        s = re.sub(r'\x00(\d+)\x00', lambda m: keep[int(m[1])], s)
    return s

# ------------------------------------------------------------------ math


def split_display(tex):
    """Stacks a display equation's top-level \\quad/\\qquad parts, so a long
    one fits a column. Environments and braces are left whole."""
    tex = tex.strip()
    tag = ''
    m = re.search(r'\\tag\{[^}]*\}\s*$', tex)
    if m:
        tag, tex = m[0], tex[:m.start()].rstrip()
    parts, depth, env, i, cur = [], 0, 0, 0, ''
    while i < len(tex):
        if tex.startswith(r'\begin{', i):
            env += 1
        elif tex.startswith(r'\end{', i):
            env -= 1
        c = tex[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        if depth == 0 and env == 0:
            m = re.match(r'\\quad\s*\\Longrightarrow\s*\\quad|\\qquad|\\quad', tex[i:])
            if m:
                parts.append(cur.strip())
                cur = r'\Longrightarrow ' if 'Longrightarrow' in m[0] else ''
                i += len(m[0])
                continue
        cur += c
        i += 1
    parts.append(cur.strip())
    if len(parts) == 1 and len(tex) > 100:
        return chain(tex, tag)
    body = tex if len(parts) == 1 else '\\begin{gathered}\n' + ' \\\\\n'.join(parts) + '\n\\end{gathered}'
    return '\n'.join(x for x in (r'\begin{equation*}', body, tag, r'\end{equation*}') if x)

def chain(tex, tag):
    """A long a = b = c, one relation per line, aligned on the =."""
    parts, depth, env, cur = [], 0, 0, ''
    for i, c in enumerate(tex):
        if tex.startswith(r'\begin{', i):
            env += 1
        elif tex.startswith(r'\end{', i):
            env -= 1
        depth += (c == '{') - (c == '}')
        if c == '=' and depth == 0 and env == 0:
            parts.append(cur.strip())
            cur = ''
        else:
            cur += c
    parts.append(cur.strip())
    body = '\\begin{aligned}\n' + parts[0] + ' &= ' + ' \\\\\n&= '.join(parts[1:]) + '\n\\end{aligned}'
    return '\n'.join(x for x in (r'\begin{equation*}', body, tag, r'\end{equation*}') if x)


# ------------------------------------------------------------------ tables


def table(rows, caption=None):
    head = [c.strip() for c in rows[0].strip().strip('|').split('|')]
    align = [c.strip() for c in rows[1].strip().strip('|').split('|')]
    body = [[c.strip() for c in r.strip().strip('|').split('|')] for r in rows[2:]]
    n = len(head)
    widest = max(len(r) for r in rows)
    content = sum(max(len(r[j]) for r in [head] + body) for j in range(n))
    wide = n >= 5 or widest > 110 or content > 62
    cols = []
    for j in range(n):
        longest = max(len(r[j]) for r in [head] + body)
        if align[j].endswith(':') and not align[j].startswith(':'):
            cols.append('r')
        elif longest > (18 if wide else 24):
            cols.append(r'>{\raggedright\arraybackslash}X')
        else:
            cols.append('l')
    width = r'\textwidth' if wide else r'\columnwidth'
    x = any('X' in c for c in cols)
    begin = rf'\begin{{tabularx}}{{{width}}}{{@{{}}{"".join(cols)}@{{}}}}' if x else rf'\begin{{tabular}}{{@{{}}{"".join(cols)}@{{}}}}'
    end = r'\end{tabularx}' if x else r'\end{tabular}'
    lines = [begin, r'\toprule',
             ' & '.join(r'\textbf{' + inline(h) + '}' if h else '' for h in head) + r' \\', r'\midrule']
    lines += [' & '.join(inline(c) for c in r) + r' \\' for r in body]
    lines += [r'\bottomrule', end]
    grid = '\n'.join(lines)
    if caption or wide:
        star = '*' if wide else ''
        cap = f'\\caption{{{caption}}}\n' if caption else ''
        return f'\\begin{{table{star}}}[t]\n\\centering\\footnotesize\n{cap}{grid}\n\\end{{table{star}}}'
    return f'\\begin{{center}}\\footnotesize\n{grid}\n\\end{{center}}'

# ------------------------------------------------------------------ blocks


def figure(path):
    pdf = 'figures/' + Path(path).with_suffix('.pdf').name
    panels = {'fig4': 0.68}.get(Path(path).name[:4], 0.84)
    return (f'\\begin{{figure*}}[t]\n\\centering\n'
            f'\\includegraphics[width={panels}\\textwidth]{{{pdf}}}\n\\end{{figure*}}')


def lists(lines):
    """Bulleted and numbered lists, nested by indent, items over several lines."""
    out, stack = [], []  # stack of (indent, env)
    for line in lines:
        m = re.match(r'^(\s*)(- |\d+\. )(.*)$', line)
        if m:
            ind, env = len(m[1]), 'itemize' if m[2] == '- ' else 'enumerate'
            while stack and stack[-1][0] > ind:
                out.append(f'\\end{{{stack.pop()[1]}}}')
            if not stack or stack[-1][0] < ind:
                stack.append((ind, env))
                out.append(f'\\begin{{{env}}}')
            out.append(['item', m[3]])
        else:
            out[-1][1] += ' ' + line.strip()
    while stack:
        out.append(f'\\end{{{stack.pop()[1]}}}')
    return '\n'.join(r'\item ' + inline(l[1]) if isinstance(l, list) else l for l in out)


def convert(md):
    lines = md.split('\n')
    out, i, caption, in_refs, in_abstract = [], 0, None, False, False
    para = []

    def flush():
        nonlocal para
        if para:
            out.append(inline(' '.join(l.strip() for l in para)))
            out.append('')
            para = []

    while i < len(lines):
        line = lines[i]
        # The title block: the first heading, the subtitle and the byline.
        if i == 0:
            name, org, rest = re.match(r'^\*\*(.+?)\*\* \((.+?)\) · (.*)$', lines[4]).groups()
            out.append(TITLE.format(title=inline(line[2:]), subtitle=inline(lines[2][4:]), name=inline(name),
                                    org=inline(org), rest=inline(rest), licence=inline(lines[6])))
            i = lines.index('---') + 1
            continue
        if line.strip() == '---':
            flush()
            if in_abstract:
                out.append(r'\end{abstract}' + '\n')
                in_abstract = False
            i += 1
            continue
        m = re.match(r'^## (\d+)\. (.*)$', line)
        if m:
            flush()
            n, name = int(m[1]), m[2]
            if n == 0:
                out.append(r'\begin{abstract}')
                in_abstract = True
            else:
                if name == 'Appendix':
                    out.append(r'\renewcommand{\thesubsection}{\Alph{subsection}}'
                               r'\renewcommand{\thesubsectiondis}{\Alph{subsection}.}')
                out.append(f'\\setcounter{{section}}{{{n - 1}}}\n\\section{{{inline(name)}}}')
            i += 1
            continue
        if line.startswith('## References'):
            flush()
            in_refs = True
            out.append(r'\bigskip\noindent')
            i += 1
            continue
        m = re.match(r'^### (\d+)\.(\d+) (.*)$', line) or re.match(r'^### ([A-Z])\. (.*)$', line)
        if m:
            flush()
            if m.lastindex == 3:
                k, name = int(m[2]), m[3]
            else:
                k, name = ord(m[1]) - ord('A') + 1, m[2]
            out.append(f'\\setcounter{{subsection}}{{{k - 1}}}\n\\subsection{{{inline(name)}}}')
            i += 1
            continue
        if line.startswith('```'):
            flush()
            lang = line[3:].strip()
            j = lines.index('```', i + 1)
            code = '\n'.join(lines[i + 1:j])
            style = {'rust': 'Rust', 'sh': 'bash'}[lang]
            out.append(f'\\begin{{lstlisting}}[language={style}]\n{code}\n\\end{{lstlisting}}\n')
            i = j + 1
            continue
        if line.startswith('$$'):
            flush()
            j = i + 1
            while not lines[j].startswith('$$'):
                j += 1
            out.append(split_display('\n'.join(lines[i + 1:j])) + '\n')
            i = j + 1
            continue
        if line.startswith('|'):
            flush()
            j = i
            while j < len(lines) and lines[j].startswith('|'):
                j += 1
            out.append(table(lines[i:j], caption) + '\n')
            caption = None
            i = j
            continue
        m = re.match(r'^!\[[^\]]*\]\(([^)]+)\)$', line)
        if m:
            flush()
            out.append(figure(m[1]) + '\n')
            i += 1
            continue
        m = re.match(r'^\*\*Table (\d+)\.\*\* (.*)$', line)
        if m:
            flush()
            caption = inline(m[2])
            # The note under the caption line belongs to the caption.
            j = i + 1
            while not lines[j].startswith('|'):
                if lines[j].strip():
                    caption += ' ' + inline(lines[j].strip())
                j += 1
            i = j
            continue
        if re.match(r'^(- |\d+\. )', line):
            flush()
            j = i
            while j < len(lines) and lines[j].strip() and (re.match(r'^(\s*)(- |\d+\. )', lines[j]) or lines[j].startswith(' ')):
                j += 1
            out.append(lists(lines[i:j]) + '\n')
            i = j
            continue
        if in_refs and re.match(r'^\[(\d+|S\d)\] ', line):
            flush()
            if not out[-1].startswith(r'\begin{thebibliography}') and r'\bibitem' not in out[-1]:
                out.append(r'\begin{thebibliography}{99}')
            k, text = re.match(r'^\[(\d+|S\d)\] (.*)$', line).groups()
            key = k if k.startswith('S') else f'r{k}'
            label = f'[{k}]' if k.startswith('S') else ''
            out.append(f'\\bibitem{label}{{{key}}} {inline(text)}')
            i += 1
            continue
        if not line.strip():
            flush()
        else:
            para.append(line)
        i += 1
    flush()
    if in_refs:
        out.append(r'\end{thebibliography}')
    return PREAMBLE + '\n'.join(out) + '\n\\end{document}\n'


PREAMBLE = r"""% SPDX-License-Identifier: GPL-3.0-or-later
% Copyright (C) 2026 Torben Gräber
%
% GENERATED by tex/md2tex.py from ../paper.md. Edit the Markdown and
% regenerate (tex/build.sh); changes made here are overwritten.
\documentclass[conference]{IEEEtran}
\usepackage{iftex}
\ifPDFTeX
  \usepackage[utf8]{inputenc}
  \usepackage[T1]{fontenc}
\else
  % By file name, which XeTeX finds in any TeX distribution (Tectonic's too).
  \usepackage{fontspec}
  \setmainfont{texgyretermes}[Extension=.otf, UprightFont=*-regular,
    BoldFont=*-bold, ItalicFont=*-italic, BoldItalicFont=*-bolditalic]
  \setsansfont{texgyreheros}[Extension=.otf, UprightFont=*-regular,
    BoldFont=*-bold, ItalicFont=*-italic, BoldItalicFont=*-bolditalic]
  \setmonofont{lmmono10-regular.otf}[BoldFont=lmmonolt10-bold.otf,
    ItalicFont=lmmono10-italic.otf, Scale=MatchLowercase]
\fi
\usepackage{amsmath,amssymb}
\usepackage{graphicx}
\usepackage{booktabs,tabularx,array}
\usepackage{xcolor}
\usepackage{listings}
\usepackage{newunicodechar}
\usepackage[hidelinks]{hyperref}
\usepackage{xurl}

% The paper's own numbering: arabic sections, subsections such as 3.0.
\renewcommand{\thesection}{\arabic{section}}
\renewcommand{\thesubsection}{\thesection.\arabic{subsection}}
\renewcommand{\thetable}{\arabic{table}}
% IEEEtran sets its heading numbers (\the...dis) at \begin{document}.
\AtBeginDocument{\renewcommand{\thesubsectiondis}{\thesection.\arabic{subsection}}}

% Symbols the text uses, in either engine.
\newunicodechar{−}{\ensuremath{-}}
\newunicodechar{≈}{\ensuremath{\approx}}
\newunicodechar{≤}{\ensuremath{\leq}}
\newunicodechar{→}{\ensuremath{\rightarrow}}
\newunicodechar{×}{\ensuremath{\times}}
\newunicodechar{±}{\ensuremath{\pm}}
\newunicodechar{µ}{\ensuremath{\mu}}
\newunicodechar{·}{\ensuremath{\cdot}}
\newunicodechar{½}{\ensuremath{\tfrac{1}{2}}}
\newunicodechar{♯}{\ensuremath{\sharp}}

% Listings: compact, breaking long lines, comments in grey. The greys are
% named: a mix such as black!50 inside postbreak breaks listings.
\definecolor{codebreak}{gray}{0.5}
\definecolor{codecomment}{gray}{0.4}
\definecolor{codetype}{gray}{0.25}
\lstdefinelanguage{Rust}{
  morekeywords={as,break,const,continue,crate,dyn,else,enum,extern,false,fn,for,
    if,impl,in,let,loop,match,mod,move,mut,pub,ref,return,self,Self,static,
    struct,super,trait,true,type,unsafe,use,where,while},
  morekeywords=[2]{f32,f64,usize,u8,u64,i32,bool,Option,Some,None,Vec},
  sensitive=true,
  morecomment=[l]{//},
  morecomment=[s]{/*}{*/},
  morestring=[b]",
}
\lstset{
  basicstyle=\ttfamily\scriptsize,
  keywordstyle=\bfseries,
  keywordstyle=[2]\color{codetype},
  commentstyle=\itshape\color{codecomment},
  columns=fullflexible, keepspaces=true,
  breaklines=true, breakindent=1.5em,
  postbreak=\mbox{\textcolor{codebreak}{$\hookrightarrow$}\space},
  frame=tb, framerule=0.3pt, aboveskip=0.6em, belowskip=0.6em,
  xleftmargin=0pt, showstringspaces=false, tabsize=4,
}

\begin{document}
"""

TITLE = r"""\title{{{title}\\[0.4em]\parbox{{0.86\textwidth}}{{\centering\large {subtitle}}}}}
\author{{\IEEEauthorblockN{{{name}}}
\IEEEauthorblockA{{{org}\\
{rest}\\
{licence}}}}}
\maketitle
"""

if __name__ == '__main__':
    src, dst = map(Path, sys.argv[1:3])
    dst.write_text(convert(src.read_text()))
