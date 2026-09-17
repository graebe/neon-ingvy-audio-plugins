//! The Move pad layout: what each pad sounds, and what it is doing in a chord.

use music_core::abletonmove::{COLUMNS, DEFAULT_ROW_OFFSET, PADS, PadGrid, PadRole, ROWS};
use music_core::{Pitch, Triad};
use rstest::rstest;

/// C major from middle C, which is what every hand-checked number below assumes.
fn c_major_grid() -> PadGrid {
    PadGrid::new(Pitch::C, 4)
}

#[test]
fn the_grid_is_four_rows_of_eight() {
    assert_eq!(ROWS, 4);
    assert_eq!(COLUMNS, 8);
    assert_eq!(PADS, 32);
}

#[test]
fn a_grid_is_small_and_copy() {
    // Four bytes, like everything else in the crate. If this grows, the docs
    // have started lying about what it costs to pass one around.
    assert_eq!(size_of::<PadGrid>(), 4);
    let grid = c_major_grid();
    let copy = grid;
    assert_eq!(grid, copy);
}

#[rstest]
#[case(0, 0, 60)] // C4, the tonic
#[case(0, 1, 62)] // D4
#[case(0, 2, 64)] // E4
#[case(0, 6, 71)] // B4, the seventh
#[case(0, 7, 72)] // C5, the octave
fn a_row_walks_up_the_scale(#[case] row: usize, #[case] column: usize, #[case] midi: i16) {
    assert_eq!(c_major_grid().note_at(row, column).midi(), midi);
}

#[rstest]
#[case(0, 60, 1)] // C4, the tonic
#[case(1, 65, 4)] // F4
#[case(2, 71, 7)] // B4
#[case(3, 76, 3)] // E5, having wrapped past the seventh
fn each_row_starts_three_degrees_above_the_last(
    #[case] row: usize,
    #[case] midi: i16,
    #[case] degree: u8,
) {
    let grid = c_major_grid();
    assert_eq!(grid.note_at(row, 0).midi(), midi);
    assert_eq!(grid.degree_at(row, 0), degree);
}

#[test]
fn the_step_between_rows_is_diatonic_not_chromatic() {
    // Three scale degrees is a fourth by name, but F to B is six semitones where
    // C to F is five. Counting in degrees is the point of playing in key.
    let grid = c_major_grid();
    let starts: [i16; ROWS] = core::array::from_fn(|r| grid.note_at(r, 0).midi());
    let steps: [i16; 3] = core::array::from_fn(|r| starts[r + 1] - starts[r]);
    assert_eq!(steps, [5, 6, 5]);
}

#[test]
fn the_default_row_offset_is_three_degrees() {
    assert_eq!(DEFAULT_ROW_OFFSET, 3);
    let grid = c_major_grid();
    assert_eq!(grid.row_offset(), 3);
    // Three degrees up from the first is the fourth.
    assert_eq!(grid.degree_at(1, 0), 4);
}

#[test]
fn a_row_rises_from_left_to_right() {
    let grid = c_major_grid();
    for row in 0..ROWS {
        for column in 1..COLUMNS {
            assert!(
                grid.note_at(row, column).midi() > grid.note_at(row, column - 1).midi(),
                "row {row} does not rise across column {column}",
            );
        }
    }
}

#[test]
fn each_row_starts_above_the_one_below_it() {
    let grid = c_major_grid();
    for row in 1..ROWS {
        assert!(grid.note_at(row, 0).midi() > grid.note_at(row - 1, 0).midi());
    }
}

#[test]
fn the_rows_overlap() {
    // A row spans eight degrees but the next starts only three up, so the same
    // note appears on several pads. That overlap is the layout, not a fault:
    // it is what lets a shape be played without reaching across the grid.
    let grid = c_major_grid();
    assert!(grid.note_at(1, 0).midi() < grid.note_at(0, COLUMNS - 1).midi());
}

#[test]
fn notes_agrees_with_note_at() {
    let grid = c_major_grid();
    let notes = grid.notes();
    for row in 0..ROWS {
        for column in 0..COLUMNS {
            assert_eq!(notes[row * COLUMNS + column], grid.note_at(row, column));
        }
    }
}

#[test]
fn every_pad_is_in_the_key() {
    // Playing in key is the whole point: no pad should sound an accidental.
    let scale = [0, 2, 4, 5, 7, 9, 11];
    for tonic in Pitch::ALL {
        let grid = PadGrid::new(tonic, 4);
        for note in grid.notes() {
            let degree = (note.pitch().value() + 12 - tonic.value()) % 12;
            assert!(
                scale.contains(&degree),
                "{tonic} grid sounds a note outside the scale",
            );
        }
    }
}

#[test]
fn degrees_run_one_through_seven_and_wrap() {
    let grid = c_major_grid();
    let degrees: [u8; COLUMNS] = core::array::from_fn(|c| grid.degree_at(0, c));
    assert_eq!(degrees, [1, 2, 3, 4, 5, 6, 7, 1]);
}

#[rstest]
#[case(0, Some(PadRole::Root))]
#[case(1, None)]
#[case(2, Some(PadRole::Third))]
#[case(3, None)]
#[case(4, Some(PadRole::Fifth))]
#[case(5, None)]
fn a_triad_lies_on_every_other_pad_of_a_row(
    #[case] column: usize,
    #[case] expected: Option<PadRole>,
) {
    assert_eq!(
        c_major_grid().role_at(0, column, Triad::major(Pitch::C)),
        expected,
    );
}

#[test]
fn a_role_holds_across_octaves() {
    let grid = c_major_grid();
    let c_major = Triad::major(Pitch::C);
    // C5 is the eighth pad of the bottom row and still the root.
    assert_eq!(grid.note_at(0, 7).midi(), 72);
    assert_eq!(grid.role_at(0, 7, c_major), Some(PadRole::Root));
}

#[test]
fn exactly_the_chord_tones_light_up() {
    let grid = c_major_grid();
    for triad in Triad::ALL {
        for row in 0..ROWS {
            for column in 0..COLUMNS {
                let note = grid.note_at(row, column);
                let role = grid.role_at(row, column, triad);
                assert_eq!(
                    role.is_some(),
                    triad.contains(note.pitch()),
                    "{triad} disagrees with itself about {note}",
                );
            }
        }
    }
}

#[test]
fn roles_index_the_chord_in_order() {
    assert_eq!(PadRole::Root.index(), 0);
    assert_eq!(PadRole::Third.index(), 1);
    assert_eq!(PadRole::Fifth.index(), 2);
}

#[test]
fn the_row_offset_is_adjustable() {
    // Two degrees stacks the rows in thirds, putting a triad in a column.
    let grid = PadGrid::new(Pitch::C, 4).with_row_offset(2);
    let c_major = Triad::major(Pitch::C);
    assert_eq!(grid.role_at(0, 0, c_major), Some(PadRole::Root));
    assert_eq!(grid.role_at(1, 0, c_major), Some(PadRole::Third));
    assert_eq!(grid.role_at(2, 0, c_major), Some(PadRole::Fifth));
}

#[test]
fn octaves_shift_the_whole_grid() {
    let low = PadGrid::new(Pitch::C, 3);
    let high = PadGrid::new(Pitch::C, 4);
    for row in 0..ROWS {
        for column in 0..COLUMNS {
            assert_eq!(
                high.note_at(row, column).midi() - low.note_at(row, column).midi(),
                12,
            );
        }
    }
}

#[test]
fn a_grid_reports_what_it_was_built_from() {
    let grid = PadGrid::new(Pitch::F_SHARP, 2).with_row_offset(5);
    assert_eq!(grid.tonic(), Pitch::F_SHARP);
    assert_eq!(grid.octave(), 2);
    assert_eq!(grid.row_offset(), 5);
}
