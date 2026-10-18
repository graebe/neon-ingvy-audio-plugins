// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters, all of them choices: one table the shell declares from.

EVERY PARAMETER IS A LIST OF NAMED CHOICES (an ni_params::Kind::Choice), so a
value is an index and there is nothing to parse, clamp or round on the audio
thread. The shell declares each from this table through the shared adapter
(plugins/_shared/juce/EngineParameter.h); the host stores the index.

TWO KINDS. The first four change what the engine reports -- the key, how it
spells, and whether a chord stays after the keys come up -- and are
automatable. The last three are the window's: which history it draws, how much
of it, and at what size. They are saved with the plugin like the others, so a
set reopens as it was left, but nothing is gained by automating a zoom, so a
host is told not to offer it. The engine keeps their values only so that one
table describes the whole state.

APPEND ONLY. The index is what a host stores in a session, so inserting a
parameter would re-point every set saved before it.
*/

/// The parameters, in the order the shell declares them: a host index IS this
/// value.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Param {
    /// The key's tonic, C to B.
    Tonic = 0,
    /// The key's mode, Ionian to Locrian.
    Mode,
    /// How the black keys are named: from the key, or always one way.
    Spelling,
    /// Whether the last reading stays after every key is released.
    Hold,
    /// The history as notation or as a piano roll.
    HistoryView,
    /// How many bars of history the window shows.
    HistorySpan,
    /// The window's size.
    Zoom,
}

/// How many parameters there are.
pub const PARAM_COUNT: usize = 7;

pub use ni_params::Kind;

/// What a shell needs to declare one parameter.
#[derive(Clone, Copy, Debug)]
pub struct Info {
    /// The stable identifier: what a saved state calls it.
    pub key: &'static str,
    /// The name a host shows.
    pub name: &'static str,
    /// Its labels, default and whether a host may automate it: always a
    /// [`Kind::Choice`].
    pub kind: Kind,
}

/// A choice of `labels`, starting at `default`.
const fn choice(labels: &'static [&'static str], default: usize, automatable: bool) -> Kind {
    Kind::Choice { labels, default, automatable }
}

const TONICS: [&str; 12] = [
    "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B",
];
const MODES: [&str; 7] = [
    "Ionian",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Aeolian",
    "Locrian",
];

impl Param {
    /// Every parameter, in index order: `ALL[i] as i32 == i`.
    pub const ALL: [Param; PARAM_COUNT] = [
        Param::Tonic,
        Param::Mode,
        Param::Spelling,
        Param::Hold,
        Param::HistoryView,
        Param::HistorySpan,
        Param::Zoom,
    ];

    /// The parameter at a host index, if there is one.
    pub fn from_i32(index: i32) -> Option<Param> {
        usize::try_from(index)
            .ok()
            .and_then(|i| Param::ALL.get(i))
            .copied()
    }

    /// The parameter a saved state calls `key`, if any.
    pub fn from_key(key: &str) -> Option<Param> {
        Param::ALL.into_iter().find(|p| p.info().key == key)
    }

    /// Everything a shell declares this parameter with.
    pub const fn info(self) -> Info {
        match self {
            Param::Tonic => Info {
                key: "tonic",
                name: "Key",
                kind: choice(&TONICS, 0, true),
            },
            Param::Mode => Info {
                key: "mode",
                name: "Mode",
                kind: choice(&MODES, 0, true),
            },
            Param::Spelling => Info {
                key: "spelling",
                name: "Spelling",
                kind: choice(&["Auto", "Sharps", "Flats"], 0, true),
            },
            Param::Hold => Info {
                key: "hold",
                name: "Hold",
                kind: choice(&["Off", "On"], 0, true),
            },
            Param::HistoryView => Info {
                key: "history_view",
                name: "History",
                kind: choice(&["Staff", "MIDI"], 0, false),
            },
            Param::HistorySpan => Info {
                key: "history_span",
                name: "Span",
                kind: choice(&["1 bar", "2 bars", "4 bars", "8 bars"], 2, false),
            },
            Param::Zoom => Info {
                key: "zoom",
                name: "Zoom",
                kind: choice(&["75%", "100%", "125%", "150%"], 1, false),
            },
        }
    }

    /// How many choices it has.
    pub const fn choices(self) -> u8 {
        match self.info().kind {
            Kind::Choice { labels, .. } => labels.len() as u8,
            Kind::Float { .. } => unreachable!(),
        }
    }

    /// The choice a new instance starts at.
    pub const fn default(self) -> u8 {
        match self.info().kind {
            Kind::Choice { default, .. } => default as u8,
            Kind::Float { .. } => unreachable!(),
        }
    }

    /// `choice` if it is one of this parameter's, otherwise the nearest that is.
    pub const fn clamp(self, choice: i32) -> u8 {
        let last = self.choices() as i32 - 1;
        if choice < 0 {
            0
        } else if choice > last {
            last as u8
        } else {
            choice as u8
        }
    }
}

/// The bars [`Param::HistorySpan`]'s choice stands for.
pub const fn span_bars(choice: u8) -> u8 {
    match choice {
        0 => 1,
        1 => 2,
        2 => 4,
        _ => 8,
    }
}

/// The scale [`Param::Zoom`]'s choice stands for, in percent.
pub const fn zoom_percent(choice: u8) -> u16 {
    match choice {
        0 => 75,
        1 => 100,
        2 => 125,
        _ => 150,
    }
}
