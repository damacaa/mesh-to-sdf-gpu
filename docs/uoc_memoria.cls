\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{uoc_memoria}[2026/10/24 v1.0 UOC Master Thesis Document Class]

% Base class
\LoadClass[11pt,a4paper]{article}

% Geometry
\RequirePackage[
  a4paper,
  left=30mm,
  right=30mm,
  top=25mm,
  bottom=25mm,
  headheight=18mm,
  headsep=6mm,
  footskip=12.5mm
]{geometry}

% Colors
\RequirePackage{xcolor}
\definecolor{uoccyan}{HTML}{73EDFF}
\definecolor{uocblue}{HTML}{000078}
\definecolor{tablegray}{HTML}{E6E6E6}
\definecolor{guidanceyellow}{HTML}{FFFF00}
\definecolor{shadecolor}{HTML}{FFFF00}
\definecolor{uocbordergray}{HTML}{CCCCCC}

% Math packages
\RequirePackage{amsmath}
\RequirePackage{amssymb}

% Font and Language support (XeLaTeX)
\RequirePackage{fontspec}
\RequirePackage[spanish,es-tabla]{babel}

\newcommand{\uoc@loadarialfrom}[1]{%
  \setmainfont{Arial}[
    Path = #1,
    Extension = .ttf,
    UprightFont = Arial,
    BoldFont = Arialbd,
    ItalicFont = Ariali,
    BoldItalicFont = Arialbi
  ]%
  \setsansfont{Arial}[
    Path = #1,
    Extension = .ttf,
    UprightFont = Arial,
    BoldFont = Arialbd,
    ItalicFont = Ariali,
    BoldItalicFont = Arialbi
  ]%
}

\IfFileExists{docs/fonts/Arial.ttf}{%
  \uoc@loadarialfrom{docs/fonts/}%
}{%
  \IfFileExists{fonts/Arial.ttf}{%
    \uoc@loadarialfrom{fonts/}%
  }{%
    \IfFontExistsTF{Arial}{%
      \setmainfont{Arial}[
        BoldFont={Arial Bold},
        ItalicFont={Arial Italic},
        BoldItalicFont={Arial Bold Italic}
      ]%
      \setsansfont{Arial}[
        BoldFont={Arial Bold},
        ItalicFont={Arial Italic},
        BoldItalicFont={Arial Bold Italic}
      ]%
    }{%
      \IfFontExistsTF{Liberation Sans}{%
        \setmainfont{Liberation Sans}[
          BoldFont={Liberation Sans Bold},
          ItalicFont={Liberation Sans Italic},
          BoldItalicFont={Liberation Sans Bold Italic}
        ]%
        \setsansfont{Liberation Sans}[
          BoldFont={Liberation Sans Bold},
          ItalicFont={Liberation Sans Italic},
          BoldItalicFont={Liberation Sans Bold Italic}
        ]%
      }{%
        \setmainfont{DejaVu Sans}%
        \setsansfont{DejaVu Sans}%
      }%
    }%
  }%
}
\renewcommand{\familydefault}{\sfdefault}

% Line spacing and paragraphs
\RequirePackage{setspace}
\setstretch{1.15}
\RequirePackage{parskip}
\setlength{\parskip}{6pt plus 1pt minus 1pt}
\setlength{\parindent}{0pt}

% Highlighting with framed (supports multiline and formatting)
\RequirePackage{framed}
\setlength{\FrameSep}{2pt}
\setlength{\FrameRule}{0pt}

\newenvironment{guidancebox}{%
  \par\vspace{1pt}\noindent%
  \begin{snugshade*}%
}{%
  \end{snugshade*}\par\vspace{2pt}%
}
\newcommand{\guidanceinline}[1]{\colorbox{guidanceyellow}{#1}}
\newcommand{\guidanceblock}[1]{\colorbox{guidanceyellow}{\parbox{\dimexpr\linewidth-2\fboxsep\relax}{#1}}}
\newcommand{\guidance}[1]{\guidanceinline{#1}}

% Graphics & Tables
\RequirePackage{graphicx}
\RequirePackage{tabularx}
\RequirePackage{colortbl}
\RequirePackage{array}
\RequirePackage{calc}
\RequirePackage{tikz}

% Headers and Footers
\RequirePackage{fancyhdr}

% Captions
\RequirePackage{caption}
\captionsetup[figure]{font={small,sf},labelfont=bf,labelsep=colon,justification=centering,skip=8pt}
\captionsetup[table]{font={small,sf},labelfont=bf,labelsep=colon,justification=centering,skip=8pt}
\DeclareCaptionFormat{guidance}{\colorbox{guidanceyellow}{#1#2#3}}

% Section headings formatting using standard LaTeX kernel macros
\renewcommand{\@seccntformat}[1]{\csname the#1\endcsname.\quad}

\renewcommand\section{\@startsection {section}{1}{\z@}%
  {-18pt \@plus -4pt \@minus -2pt}%
  {12pt \@plus 2pt \@minus 2pt}%
  {\fontsize{20}{24}\bfseries}}

\renewcommand\subsection{\@startsection{subsection}{2}{\z@}%
  {-14pt \@plus -3pt \@minus -2pt}%
  {6pt \@plus 1pt \@minus 1pt}%
  {\fontsize{12}{15}\bfseries}}

\renewcommand\subsubsection{\@startsection{subsubsection}{3}{\z@}%
  {-12pt \@plus -2pt \@minus -1pt}%
  {4pt \@plus 1pt \@minus 1pt}%
  {\fontsize{12}{15}\bfseries}}

% Numbering format
\renewcommand{\thesection}{\arabic{section}}
\renewcommand{\thesubsection}{\arabic{section}.\arabic{subsection}}
\renewcommand{\thesubsubsection}{\arabic{section}.\arabic{subsection}.\arabic{subsubsection}}

% Table of Contents dots for sections
\renewcommand*\l@section[2]{%
  \ifnum \c@tocdepth >\z@
    \addpenalty\@secpenalty
    \setlength\@tempdima{1.8em}%
    \begingroup
      \parindent \z@ \rightskip \@pnumwidth
      \parfillskip -\@pnumwidth
      \leavevmode
      \advance\leftskip\@tempdima
      \hskip -\leftskip
      #1\nobreak\leaders\hbox{$\m@th\mkern \@dotsep mu\hbox{.}\mkern \@dotsep mu$}\hfill
      \nobreak\hb@xt@\@pnumwidth{\hss #2}\par
    \endgroup
  \fi}

% Hyperref
\PassOptionsToPackage{hyphens}{url}
\RequirePackage[hidelinks]{hyperref}
\hypersetup{
  colorlinks=true,
  linkcolor=blue,
  filecolor=blue,
  urlcolor=blue,
  citecolor=blue
}

% Variables for Title and Project metadata
\newcommand{\@tfmtitlefont}{\fontsize{46}{54}}
\newcommand{\tfmtitlefont}[2]{\renewcommand{\@tfmtitlefont}{\fontsize{#1}{#2}}}

\newcommand{\@tfmsubtitlefont}{\fontsize{26}{32}}
\newcommand{\tfmsubtitlefont}[2]{\renewcommand{\@tfmsubtitlefont}{\fontsize{#1}{#2}}}

\newcommand{\@tfmsubtitle}{}
\newcommand{\tfmsubtitle}[1]{\renewcommand{\@tfmsubtitle}{#1}}

\newcommand{\@tfmstudent}{}
\newcommand{\tfmstudent}[1]{\renewcommand{\@tfmstudent}{#1}}

\newcommand{\@tfmprogram}{}
\newcommand{\tfmprogram}[1]{\renewcommand{\@tfmprogram}{#1}}

\newcommand{\@tfmarea}{}
\newcommand{\tfmarea}[1]{\renewcommand{\@tfmarea}{#1}}

\newcommand{\@tfmtutor}{}
\newcommand{\tfmtutor}[1]{\renewcommand{\@tfmtutor}{#1}}

\newcommand{\@tfmpra}{}
\newcommand{\tfmpra}[1]{\renewcommand{\@tfmpra}{#1}}

\newcommand{\@tfmdate}{}
\newcommand{\tfmdate}[1]{\renewcommand{\@tfmdate}{#1}}

\newcommand{\@tfmlanguage}{Castellano}
\newcommand{\tfmlanguage}[1]{\renewcommand{\@tfmlanguage}{#1}}

\newcommand{\@tfmkeywords}{}
\newcommand{\tfmkeywords}[1]{\renewcommand{\@tfmkeywords}{#1}}

\newcommand{\@tfmabstractes}{}
\newcommand{\tfmabstractes}[1]{\renewcommand{\@tfmabstractes}{#1}}

\newcommand{\@tfmabstracten}{}
\newcommand{\tfmabstracten}[1]{\renewcommand{\@tfmabstracten}{#1}}

% Optional Ficha overrides (falls back to main metadata if not set)
\newcommand{\@fichatitle}{}
\newcommand{\fichatitle}[1]{\renewcommand{\@fichatitle}{#1}}
\newcommand{\@fichastudent}{}
\newcommand{\fichastudent}[1]{\renewcommand{\@fichastudent}{#1}}
\newcommand{\@fichatutor}{}
\newcommand{\fichatutor}[1]{\renewcommand{\@fichatutor}{#1}}
\newcommand{\@fichapra}{}
\newcommand{\fichapra}[1]{\renewcommand{\@fichapra}{#1}}
\newcommand{\@fichadate}{}
\newcommand{\fichadate}[1]{\renewcommand{\@fichadate}{#1}}
\newcommand{\@fichaprogram}{}
\newcommand{\fichaprogram}[1]{\renewcommand{\@fichaprogram}{#1}}
\newcommand{\@fichaarea}{}
\newcommand{\fichaarea}[1]{\renewcommand{\@fichaarea}{#1}}
\newcommand{\@fichalanguage}{}
\newcommand{\fichalanguage}[1]{\renewcommand{\@fichalanguage}{#1}}
\newcommand{\@fichakeywords}{}
\newcommand{\fichakeywords}[1]{\renewcommand{\@fichakeywords}{#1}}

% Path to template assets
\newcommand{\templateassets}{docs/template_assets}

% Page Styles
\fancypagestyle{uocdefault}{
  \fancyhf{}
  \fancyhead[L]{\makebox[\textwidth][l]{\hspace*{-8mm}\includegraphics[width=170.1mm]{\templateassets/uoc_header.png}}}
  \renewcommand{\headrulewidth}{0pt}
  \fancyfoot[C]{\fontsize{10}{12}\selectfont\thepage}
  \renewcommand{\footrulewidth}{0pt}
}

\fancypagestyle{uocplain}{
  \fancyhf{}
  \fancyhead{}
  \renewcommand{\headrulewidth}{0pt}
  \fancyfoot[C]{\fontsize{10}{12}\selectfont\thepage}
  \renewcommand{\footrulewidth}{0pt}
}

\fancypagestyle{licensepage}{
  \fancyhf{}
  \fancyhead[L]{\makebox[\textwidth][l]{\hspace*{-8mm}\includegraphics[width=170.1mm]{\templateassets/uoc_header.png}}}
  \renewcommand{\headrulewidth}{0pt}
  \fancyfoot[L]{%
    \fontsize{10.5}{13}\selectfont\textcolor{uocblue}{%
    \textcolor{uoccyan}{\rule{\textwidth}{3pt}}\\%
    \@tfmprogram\hfill\@tfmdate}%
  }
  \renewcommand{\footrulewidth}{0pt}
}

% Cover page command
\newcommand{\makecoverpage}{%
  \thispagestyle{empty}%
  \begin{tikzpicture}[remember picture, overlay]
    % Origin at top-left of the cover box: (15mm from left, 25mm from top)
    \coordinate (boxNW) at ([xshift=15mm, yshift=-25mm]current page.north west);
    
    % 1. Top cyan box: width 185mm, height 74.2mm
    \fill[uoccyan] (boxNW) rectangle ++(185mm, -74.2mm);
    \draw[black, line width=0.5pt] (boxNW) rectangle ++(185mm, -74.2mm);
    
    % Title and Subtitle inside top cyan box
    \node[anchor=north west, inner sep=0pt, text width=160mm] at ([xshift=12mm, yshift=-8mm]boxNW) {%
      \begin{minipage}[t][58mm][s]{160mm}
        \hyphenpenalty=10000%
        {\@tfmtitlefont\bfseries\textcolor{uocblue}{\@title}\par}%
        \ifx\@tfmsubtitle\@empty\else%
          \vfill%
          {\@tfmsubtitlefont\selectfont\textcolor{uocblue}{\@tfmsubtitle}\par}%
        \fi%
      \end{minipage}%
    };
    
    % 2. Bottom box: width 185mm, height 172.8mm
    \coordinate (bottomNW) at ([yshift=-74.2mm]boxNW);
    \draw[black, line width=0.5pt] (bottomNW) rectangle ++(185mm, -172.8mm);
    
    % Vertical separator at 62.5mm
    \draw[black, line width=0.5pt] ([xshift=62.5mm]bottomNW) -- ++(0, -172.8mm);
    
    % Left cell: UOC Logo
    \node[anchor=north west, inner sep=0pt] at ([xshift=4mm, yshift=-10mm]bottomNW) {%
      \includegraphics[width=56mm,height=144mm,keepaspectratio]{\templateassets/uoc_logo_cover.png}%
    };
    
    % Right cell: Metadata
    \node[anchor=north west, inner sep=0pt, text width=105mm] at ([xshift=72.5mm, yshift=-12mm]bottomNW) {%
      \hyphenpenalty=10000%
      {\fontsize{22}{28}\bfseries \@tfmstudent\par}%
      \vspace{10mm}%
      {\fontsize{18}{22}\selectfont \@tfmprogram\par}%
      {\fontsize{18}{22}\selectfont \@tfmarea\par}%
      \vspace{12mm}%
      {\fontsize{18}{22}\bfseries Nombre Tutor/a de TF\par}%
      \vspace{1.5mm}%
      {\fontsize{16}{20}\selectfont \@tfmtutor\par}%
      \vspace{5mm}%
      {\fontsize{18}{22}\bfseries Profesor/a responsable de la asignatura\par}%
      \vspace{1.5mm}%
      {\fontsize{16}{20}\selectfont \@tfmpra\par}%
      \vspace{14mm}%
      {\fontsize{18}{22}\selectfont \@tfmdate\par}%
    };
  \end{tikzpicture}%
  \clearpage%
}

% Project Sheet (Ficha del Trabajo Final) command
\newcommand{\@tfmabstractfontsize}{\fontsize{8}{9.8}\selectfont}
\newcommand{\tfmabstractfontsize}[2]{\renewcommand{\@tfmabstractfontsize}{\fontsize{#1}{#2}\selectfont}}

\newcommand{\makefichaproyecto}{%
  {\centering\fontsize{12}{14}\bfseries FICHA DEL TRABAJO FINAL\par}%
  \vspace{1.5mm}%
  \noindent%
  \setlength{\fboxsep}{0pt}%
  \setlength{\fboxrule}{1.5pt}%
  \fbox{%
    \setlength{\arrayrulewidth}{0.5pt}%
    \renewcommand{\arraystretch}{1.05}%
    \begin{tabularx}{\dimexpr\textwidth-3pt\relax}{|>{\raggedleft\arraybackslash\bfseries}p{62mm}|>{\raggedright\arraybackslash}X|}
      \hline
      Título del trabajo: & \cellcolor{tablegray} \ifx\@fichatitle\@empty\@title\else\@fichatitle\fi \\ \hline
      Nombre del autor: & \cellcolor{tablegray} \ifx\@fichastudent\@empty\@tfmstudent\else\@fichastudent\fi \\ \hline
      Nombre del tutor/a de TF: & \cellcolor{tablegray} \ifx\@fichatutor\@empty\@tfmtutor\else\@fichatutor\fi \\ \hline
      Nombre del PRA: & \cellcolor{tablegray} \ifx\@fichapra\@empty\@tfmpra\else\@fichapra\fi \\ \hline
      Fecha de entrega (mm/aaaa): & \cellcolor{tablegray} \ifx\@fichadate\@empty\@tfmdate\else\@fichadate\fi \\ \hline
      Titulación o programa: & \cellcolor{tablegray} \ifx\@fichaprogram\@empty\@tfmprogram\else\@fichaprogram\fi \\ \hline
      Área del Trabajo Final: & \cellcolor{tablegray} \ifx\@fichaarea\@empty\@tfmarea\else\@fichaarea\fi \\ \hline
      Idioma del trabajo: & \cellcolor{tablegray} \ifx\@fichalanguage\@empty\@tfmlanguage\else\@fichalanguage\fi \\ \hline
      Palabras clave & \cellcolor{tablegray} \ifx\@fichakeywords\@empty\@tfmkeywords\else\@fichakeywords\fi \\ \hline
      \multicolumn{2}{|l|}{\textbf{Resumen del Trabajo}} \\ \hline
      \multicolumn{2}{|>{\arraybackslash}p{\dimexpr\textwidth-4pt-2\tabcolsep-2\arrayrulewidth\relax}|}{\cellcolor{tablegray}\@tfmabstractfontsize \@tfmabstractes} \\ \hline
      \multicolumn{2}{|l|}{\textbf{Abstract}} \\ \hline
      \multicolumn{2}{|>{\arraybackslash}p{\dimexpr\textwidth-4pt-2\tabcolsep-2\arrayrulewidth\relax}|}{\cellcolor{tablegray}\@tfmabstractfontsize \@tfmabstracten} \\ \hline
    \end{tabularx}%
  }%
  \par
}

