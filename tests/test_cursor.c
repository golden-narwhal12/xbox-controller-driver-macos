/*******************************************************************************
 * test_cursor.c - Cursor confinement and click capture (K1)
 *
 * Pure geometry first, then the real cursor_move / cursor_button code against
 * a scripted window, cursor and display layout.
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"
#include <stdlib.h>

#define GAME_PID   77
#define GAME_WIN   1001
#define OTHER_PID  88
#define OTHER_WIN  2002

static const ScreenRect BOX = {100, 200, 300, 150};    // x 100..399, y 200..349

/* A rig confined to a 800x600 window at (100, 100). With a 2 pt margin and
   the 28 pt title bar inset the cursor may be at x 102..897, y 130..697.
   (The geometry tests use a small margin to keep the numbers readable; the
   default margin has its own tests below.) */
static void window_rig(Rig *rig) {
    rig_init(rig);
    rig->config.cursor.confine = CONFINE_WINDOW;
    rig->config.cursor.margin = 2.0f;
    fake_set_window(100, 100, 800, 600, GAME_WIN, GAME_PID);
    fake.cursor = (ScreenPoint){500, 400};
}

static bool current_rect(const Rig *rig, ScreenRect *rect) {
    return cursor_confine_rect(&fake.window, &rig->config.cursor, RIG_OWN_PID, rect);
}

static void move(Rig *rig, float dx, float dy) {
    cursor_move(&rig->cursor, &rig->config.cursor, dx, dy, MOUSE_BUTTON_NONE);
}

static void press(Rig *rig, int button) {
    cursor_button(&rig->cursor, &rig->config.cursor, button, true);
}

static void release(Rig *rig, int button) {
    cursor_button(&rig->cursor, &rig->config.cursor, button, false);
}

/*******************************************************************************
 * confine(): pure geometry
 ******************************************************************************/
TEST(confine_inside_moves_freely) {
    ScreenPoint p = cursor_confine((ScreenPoint){200, 250}, 30, -20, BOX);
    ASSERT_EQ(p.x, 230);
    ASSERT_EQ(p.y, 230);
    p = cursor_confine((ScreenPoint){200, 250}, 0, 0, BOX);
    ASSERT_EQ(p.x, 200);
    ASSERT_EQ(p.y, 250);
    TEST_PASS();
}

TEST(confine_each_edge_keeps_other_axis) {
    // Pushing into an edge stops that axis; motion along the edge continues
    ScreenPoint p = cursor_confine((ScreenPoint){100, 250}, -10, 7, BOX);     // left
    ASSERT_EQ(p.x, 100);
    ASSERT_EQ(p.y, 257);
    p = cursor_confine((ScreenPoint){399, 250}, 10, -7, BOX);                 // right
    ASSERT_EQ(p.x, 399);
    ASSERT_EQ(p.y, 243);
    p = cursor_confine((ScreenPoint){250, 200}, 9, -10, BOX);                 // top
    ASSERT_EQ(p.x, 259);
    ASSERT_EQ(p.y, 200);
    p = cursor_confine((ScreenPoint){250, 349}, -9, 10, BOX);                 // bottom
    ASSERT_EQ(p.x, 241);
    ASSERT_EQ(p.y, 349);
    // Moving away from an edge is not blocked
    p = cursor_confine((ScreenPoint){100, 200}, 5, 5, BOX);
    ASSERT_EQ(p.x, 105);
    ASSERT_EQ(p.y, 205);
    TEST_PASS();
}

TEST(confine_past_each_corner) {
    const double steps[4][2] = {{-50, -50}, {50, -50}, {-50, 50}, {50, 50}};
    const double corners[4][2] = {{100, 200}, {399, 200}, {100, 349}, {399, 349}};
    for (int i = 0; i < 4; i++) {
        ScreenPoint p = cursor_confine((ScreenPoint){corners[i][0], corners[i][1]},
                                       steps[i][0], steps[i][1], BOX);
        ASSERT_EQ(p.x, corners[i][0]);
        ASSERT_EQ(p.y, corners[i][1]);
    }
    TEST_PASS();
}

TEST(confine_delta_larger_than_rect) {
    ScreenPoint p = cursor_confine((ScreenPoint){250, 275}, 100000, -100000, BOX);
    ASSERT_EQ(p.x, 399);
    ASSERT_EQ(p.y, 200);
    p = cursor_confine((ScreenPoint){250, 275}, -1e12, 1e12, BOX);
    ASSERT_EQ(p.x, 100);
    ASSERT_EQ(p.y, 349);
    TEST_PASS();
}

/* A start point outside the rect is pulled in before the motion is applied. */
TEST(confine_start_outside_is_pulled_in) {
    ScreenPoint p = cursor_confine((ScreenPoint){-500, 9000}, 5, -5, BOX);
    ASSERT_EQ(p.x, 105);
    ASSERT_EQ(p.y, 344);
    p = cursor_clamp_to_rect((ScreenPoint){250, 0}, BOX);       // nearest point: straight down
    ASSERT_EQ(p.x, 250);
    ASSERT_EQ(p.y, 200);
    TEST_PASS();
}

/* Displays left of / above the main display have negative coordinates. */
TEST(confine_negative_coordinates) {
    const ScreenRect left_display_window = {-1800, -300, 600, 400};   // x -1800..-1201, y -300..99
    ScreenPoint p = cursor_confine((ScreenPoint){-1500, -100}, -1000, -1000, left_display_window);
    ASSERT_EQ(p.x, -1800);
    ASSERT_EQ(p.y, -300);
    p = cursor_confine((ScreenPoint){-1500, -100}, 1000, 1000, left_display_window);
    ASSERT_EQ(p.x, -1201);
    ASSERT_EQ(p.y, 99);
    p = cursor_confine((ScreenPoint){-1500, -100}, 25, -25, left_display_window);
    ASSERT_EQ(p.x, -1475);
    ASSERT_EQ(p.y, -125);
    TEST_PASS();
}

/*******************************************************************************
 * Confinement rect from a window
 ******************************************************************************/
TEST(rect_is_frame_inset_by_margin_and_title_bar) {
    Rig rig;
    window_rig(&rig);
    ScreenRect rect;
    ASSERT_TRUE(current_rect(&rig, &rect));
    ASSERT_EQ(rect.x, 102);
    ASSERT_EQ(rect.y, 130);           // 100 + 2 margin + 28 title bar
    ASSERT_EQ(rect.w, 796);
    ASSERT_EQ(rect.h, 568);

    rig.config.cursor.margin = 10.0f;
    rig.config.cursor.top_inset = 0.0f;
    ASSERT_TRUE(current_rect(&rig, &rect));
    ASSERT_EQ(rect.x, 110);
    ASSERT_EQ(rect.y, 110);
    ASSERT_EQ(rect.w, 780);
    ASSERT_EQ(rect.h, 580);
    TEST_PASS();
}

/* A fullscreen window has no title bar to protect, but the margin still
   applies: the cursor must stay off the display edge. */
TEST(rect_fullscreen_keeps_margin_drops_title_inset) {
    Rig rig;
    window_rig(&rig);
    fake_set_window(0, 0, 1920, 1080, GAME_WIN, GAME_PID);
    fake.window.fullscreen = true;
    ScreenRect rect;
    ASSERT_TRUE(current_rect(&rig, &rect));
    ASSERT_EQ(rect.x, 2);
    ASSERT_EQ(rect.y, 2);             // no 28 pt title bar inset
    ASSERT_EQ(rect.w, 1916);
    ASSERT_EQ(rect.h, 1076);
    TEST_PASS();
}

/* No usable window means "not confined to a window" (display fallback). */
TEST(rect_unusable_windows_are_rejected) {
    Rig rig;
    window_rig(&rig);
    ScreenRect rect;

    fake_set_window(100, 100, 0, 0, GAME_WIN, GAME_PID);            // zero size
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, -800, 600, GAME_WIN, GAME_PID);       // negative size
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, 800, -600, GAME_WIN, GAME_PID);
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, NAN, 600, GAME_WIN, GAME_PID);        // garbage
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, 800, 30, GAME_WIN, GAME_PID);         // nothing left after insets
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, 4, 600, GAME_WIN, GAME_PID);
    ASSERT_FALSE(current_rect(&rig, &rect));
    fake_set_window(100, 100, 6, 34, GAME_WIN, GAME_PID);           // smallest that survives
    ASSERT_TRUE(current_rect(&rig, &rect));
    fake_clear_window();                                            // no focused window
    ASSERT_FALSE(current_rect(&rig, &rect));

    fake_set_window(100, 100, 800, 600, GAME_WIN, RIG_OWN_PID);     // the driver's own window
    ASSERT_FALSE(current_rect(&rig, &rect));

    fake_set_window(100, 100, 800, 600, GAME_WIN, GAME_PID);
    rig.config.cursor.confine = CONFINE_DISPLAY;                    // not asked for
    ASSERT_FALSE(current_rect(&rig, &rect));
    rig.config.cursor.confine = CONFINE_OFF;
    ASSERT_FALSE(current_rect(&rig, &rect));
    TEST_PASS();
}

/*******************************************************************************
 * Motion
 ******************************************************************************/
TEST(move_inside_window) {
    Rig rig;
    window_rig(&rig);
    move(&rig, 10.0f, -4.0f);
    const FakeEvent *event = fake_last(FAKE_MOVE);
    ASSERT_EQ(event->position.x, 510);
    ASSERT_EQ(event->position.y, 396);
    ASSERT_EQ(event->dx, 10);
    ASSERT_EQ(event->dy, -4);
    ASSERT_EQ(fake.warps, 0);
    TEST_PASS();
}

/* The original bug: turning stalled at an edge. The delta fields must carry
   the full requested motion on every event, however long the stick is held,
   even though the position can go no further. */
TEST(delta_fields_always_carry_requested_motion) {
    Rig rig;
    window_rig(&rig);
    long delivered_x = 0, delivered_y = 0;
    for (int i = 0; i < 2000; i++) {
        move(&rig, 15.0f, 9.0f);                 // down-right, forever
        const FakeEvent *event = fake_last(FAKE_MOVE);
        ASSERT_EQ(event->dx, 15);
        ASSERT_EQ(event->dy, 9);
        delivered_x += event->dx;
        delivered_y += event->dy;
        ASSERT(event->position.x <= 897 && event->position.y <= 697);
    }
    ASSERT_EQ(fake.moves, 2000);                 // never skipped at the edge
    ASSERT_EQ(delivered_x, 30000);
    ASSERT_EQ(delivered_y, 18000);
    ASSERT_EQ(fake.cursor.x, 897);               // pinned in the corner
    ASSERT_EQ(fake.cursor.y, 697);
    TEST_PASS();
}

/* Sliding along an edge: the blocked axis stops, the free one keeps moving. */
TEST(move_slides_along_edge) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){890, 400};
    for (int i = 0; i < 10; i++) move(&rig, 5.0f, 3.0f);
    ASSERT_EQ(fake.cursor.x, 897);
    ASSERT_EQ(fake.cursor.y, 430);
    TEST_PASS();
}

TEST(subpixel_remainder_survives_clamping) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){897, 400};       // already against the right edge
    long delivered = 0;
    for (int i = 0; i < 1000; i++) {
        move(&rig, 0.3f, 0.0f);                  // 0.3 pt per tick into the edge
    }
    for (int i = 0; i < fake.log_count; i++) {
        ASSERT_EQ(fake.log[i].dx, 1);            // whole points only, never empty
        delivered += fake.log[i].dx;
    }
    ASSERT(labs(delivered - 300) <= 1);          // 1000 * 0.3, nothing lost
    ASSERT_EQ(fake.cursor.x, 897);

    // Reversing direction uses up the carried fraction before moving back
    window_rig(&rig);
    move(&rig, 0.4f, 0.0f);
    ASSERT_EQ(fake.moves, 0);
    move(&rig, -0.4f, 0.0f);
    move(&rig, -0.4f, 0.0f);
    ASSERT_EQ(fake.moves, 0);                    // net -0.4: still under half a point
    move(&rig, -0.2f, 0.0f);
    ASSERT_EQ(fake.moves, 1);
    ASSERT_EQ(fake_last(FAKE_MOVE)->dx, -1);
    TEST_PASS();
}

/* The real mouse pushed the cursor out of the window; the next controller
   motion brings it back in. */
TEST(move_from_outside_enters_window) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){1500, 50};       // right of and above the window
    move(&rig, -3.0f, 2.0f);
    const FakeEvent *event = fake_last(FAKE_MOVE);
    ASSERT_EQ(event->position.x, 894);           // 897 - 3
    ASSERT_EQ(event->position.y, 132);           // 130 + 2
    ASSERT_EQ(event->dx, -3);
    TEST_PASS();
}

/* Confinement only acts on controller events: with the stick at rest nothing
   is ever posted, wherever the real mouse goes. */
TEST(real_mouse_is_never_fought) {
    Rig rig;
    window_rig(&rig);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < 500; i++) {
        fake.cursor = (ScreenPoint){(double)(i * 4), 20};      // the user moving the mouse outside
        rig_tick(&rig, MOTION_TICK_MS);
    }
    ASSERT_EQ(fake.log_count, 0);
    ASSERT_EQ(fake.cursor.x, 499 * 4);
    TEST_PASS();
}

/* A game that has captured the cursor holds it still and reads deltas. The
   driver must keep sending them indefinitely: looking around never stalls. */
TEST(captured_cursor_keeps_turning) {
    Rig rig;
    window_rig(&rig);
    fake.cursor_frozen = true;
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, 0);   // hold the look stick right
    long turned = 0;
    for (int i = 0; i < 6000; i++) {             // one minute
        rig_tick(&rig, MOTION_TICK_MS);
    }
    for (int i = 0; i < fake.log_count; i++) turned += fake.log[i].dx;
    ASSERT(fake.moves > 5900);
    ASSERT(turned > 100000);
    ASSERT_EQ(fake.cursor.x, 500);
    TEST_PASS();
}

/*******************************************************************************
 * Window moves and resizes while the stick is held
 ******************************************************************************/
static bool every_post_inside_current_rect(Rig *rig, int from_index) {
    ScreenRect rect;
    if (!current_rect(rig, &rect)) return false;
    for (int i = from_index; i < fake.log_count; i++) {
        const FakeEvent *event = &fake.log[i];
        if ((event->type == FAKE_MOVE || event->type == FAKE_BUTTON || event->type == FAKE_WARP) &&
            !rect_contains(rect, event->position)) {
            printf("\n         event %d at (%.0f, %.0f) outside the window rect ",
                   i, event->position.x, event->position.y);
            return false;
        }
    }
    return rect_contains(rect, fake.cursor);
}

TEST(resize_and_move_sequence_stays_confined) {
    Rig rig;
    window_rig(&rig);
    fake.displays[0] = (ScreenRect){-3000, -3000, 9000, 9000};   // displays are not the limit here
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, -32768);              // hold down-right

    // Scripted window: shrink under the cursor, grow, jump away, jump back,
    // go tiny, move to negative coordinates.
    const double script[][4] = {
        {100, 100, 800, 600}, {100, 100, 400, 300}, {100, 100, 200, 120},
        {100, 100, 1200, 900}, {900, 500, 500, 400}, {-50, 40, 640, 480},
        {100, 100, 60, 60}, {-2500, -1200, 700, 500}, {300, 300, 800, 600},
    };
    int steps = (int)(sizeof(script) / sizeof(script[0]));
    for (int step = 0; step < steps; step++) {
        fake_set_window(script[step][0], script[step][1], script[step][2], script[step][3],
                        GAME_WIN, GAME_PID);
        for (int tick = 0; tick < 25; tick++) {
            int before = fake.log_count;
            rig_tick(&rig, MOTION_TICK_MS);
            ASSERT(fake.log_count > before);                     // still moving every tick
            ASSERT_TRUE(every_post_inside_current_rect(&rig, before));
        }
    }
    TEST_PASS();
}

/* The window changes on every single tick (a live resize drag). */
TEST(live_resize_every_tick_stays_confined) {
    Rig rig;
    window_rig(&rig);
    fake.displays[0] = (ScreenRect){-3000, -3000, 9000, 9000};
    uint32_t seed = 2024;
    for (int tick = 0; tick < 3000; tick++) {
        double x = (double)(test_random(&seed) % 1200) - 300.0;
        double y = (double)(test_random(&seed) % 700) - 100.0;
        double w = 80.0 + (double)(test_random(&seed) % 900);
        double h = 80.0 + (double)(test_random(&seed) % 700);
        fake_set_window(x, y, w, h, GAME_WIN, GAME_PID);
        // Stick direction changes too
        rig_report(&rig, 0, 0, 0, 0, 0,
                   (int)(test_random(&seed) % 65535) - 32767,
                   (int)(test_random(&seed) % 65535) - 32767);
        int before = fake.log_count;
        rig_tick(&rig, 10.0f + (float)(test_random(&seed) % 4));
        if (fake.log_count > before) ASSERT_TRUE(every_post_inside_current_rect(&rig, before));
        if (fake.log_count > FAKE_LOG_CAPACITY - 16) fake_clear_log();
    }
    TEST_PASS();
}

/* The published generation changes with the window; the rect used for a post
   is always the one current at that moment, never one remembered earlier. */
TEST(stale_rect_is_never_used) {
    Rig rig;
    window_rig(&rig);
    uint32_t generation = fake.window.generation;
    fake.cursor = (ScreenPoint){890, 690};
    move(&rig, 5.0f, 5.0f);
    ASSERT_EQ(fake.cursor.x, 895);

    // The window shrinks to the left; the old rect would still allow x = 895
    fake_set_window(100, 100, 300, 300, GAME_WIN, GAME_PID);
    ASSERT(fake.window.generation != generation);
    int queries = fake.window_queries;
    move(&rig, 1.0f, 1.0f);
    ASSERT(fake.window_queries > queries);       // re-read for this very event
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.x, 397);
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.y, 397);

    // The window disappears: no rect at all, so nothing clamps to the old one
    fake_clear_window();
    fake.cursor = (ScreenPoint){1000, 800};
    move(&rig, 10.0f, 0.0f);
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.x, 1010);
    TEST_PASS();
}

/*******************************************************************************
 * Clicks
 ******************************************************************************/

/* Cursor outside the window, then a click: exactly one warp into the window,
   then exactly one button-down, at that in-window point. Never outside. */
static bool click_outside_lands_inside(int button, ScreenPoint outside, ScreenPoint expected) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = outside;
    press(&rig, button);

    if (fake.log_count != 2) return false;
    const FakeEvent *warp = &fake.log[0], *down = &fake.log[1];
    return warp->type == FAKE_WARP && down->type == FAKE_BUTTON &&
           down->code == button && down->pressed &&
           warp->position.x == expected.x && warp->position.y == expected.y &&
           down->position.x == expected.x && down->position.y == expected.y &&
           fake.mouse_down[button] == 1 && every_post_inside_current_rect(&rig, 0);
}

TEST(click_outside_window_is_moved_inside) {
    // Desktop to the right, title bar, far corner, other display
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_LEFT, (ScreenPoint){1500, 400},
                                           (ScreenPoint){897, 400}));
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_LEFT, (ScreenPoint){500, 110},
                                           (ScreenPoint){500, 130}));
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_LEFT, (ScreenPoint){0, 0},
                                           (ScreenPoint){102, 130}));
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_LEFT, (ScreenPoint){-2000, 5000},
                                           (ScreenPoint){102, 697}));
    // One point outside is still outside
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_LEFT, (ScreenPoint){898, 698},
                                           (ScreenPoint){897, 697}));
    TEST_PASS();
}

TEST(right_and_middle_clicks_are_confined_too) {
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_RIGHT, (ScreenPoint){1500, 400},
                                           (ScreenPoint){897, 400}));
    ASSERT_TRUE(click_outside_lands_inside(MOUSE_BUTTON_CENTER, (ScreenPoint){50, 900},
                                           (ScreenPoint){102, 697}));
    TEST_PASS();
}

TEST(click_inside_window_does_not_warp) {
    Rig rig;
    window_rig(&rig);
    press(&rig, MOUSE_BUTTON_LEFT);
    release(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake.log_count, 2);
    ASSERT_EQ(fake.log[0].position.x, 500);
    ASSERT_EQ(fake.log[0].position.y, 400);
    ASSERT_EQ(fake.log[1].position.x, 500);
    ASSERT_EQ(fake.log[1].position.y, 400);
    ASSERT_FALSE(fake.log[1].pressed);
    TEST_PASS();
}

/* The whole path: mouse nudged outside, RT pulled (raw report bytes). */
TEST(trigger_click_from_outside_end_to_end) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){1700, 900};      // on the desktop
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);    // RT = left click
    ASSERT_EQ(fake.warps, 1);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 0));

    rig_report(&rig, 0, 1023, 1023, 0, 0, 0, 0); // LT = right click, cursor now inside
    ASSERT_EQ(fake.warps, 1);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_RIGHT], 1);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_RIGHT], 1);
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 0));
    TEST_PASS();
}

/* Button held while the stick moves: drag events, still confined. */
TEST(drag_stays_confined) {
    Rig rig;
    window_rig(&rig);
    rig_report(&rig, 0, 0, 1023, 0, 0, 32767, 0);            // RT held, stick right
    for (int i = 0; i < 300; i++) rig_tick(&rig, MOTION_TICK_MS);

    int drags = 0;
    for (int i = 0; i < fake.log_count; i++) {
        if (fake.log[i].type != FAKE_MOVE) continue;
        ASSERT_EQ(fake.log[i].drag_button, MOUSE_BUTTON_LEFT);
        drags++;
    }
    ASSERT(drags > 290);
    ASSERT_EQ(fake.cursor.x, 897);                           // stopped at the window edge
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 0));

    // The release lands where the drag ended, not back where it started
    rig_report(&rig, 0, 0, 0, 0, 0, 32767, 0);
    const FakeEvent *up = fake_last(FAKE_BUTTON);
    ASSERT_FALSE(up->pressed);
    ASSERT_EQ(up->position.x, 897);
    ASSERT_EQ(up->position.y, 400);
    TEST_PASS();
}

/* The window moves between press and release: the release lands on the same
   spot of the window, so it is still a click on the same thing. */
TEST(release_follows_moved_window) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){300, 350};                   // (200, 250) from the window origin
    press(&rig, MOUSE_BUTTON_LEFT);
    fake_set_window(640, 20, 800, 600, GAME_WIN, GAME_PID);  // moved by (+540, -80)
    release(&rig, MOUSE_BUTTON_LEFT);

    const FakeEvent *up = fake_last(FAKE_BUTTON);
    ASSERT_FALSE(up->pressed);
    ASSERT_EQ(up->position.x, 840);
    ASSERT_EQ(up->position.y, 270);
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 1));

    // The window shrank instead, leaving the press point outside it: the
    // release is clamped into what is left
    window_rig(&rig);
    fake.cursor = (ScreenPoint){800, 650};
    press(&rig, MOUSE_BUTTON_LEFT);
    fake_set_window(100, 100, 300, 300, GAME_WIN, GAME_PID);
    release(&rig, MOUSE_BUTTON_LEFT);
    up = fake_last(FAKE_BUTTON);
    ASSERT_EQ(up->position.x, 397);
    ASSERT_EQ(up->position.y, 397);
    TEST_PASS();
}

/* Cmd-Tab while RT is held. The release must be delivered, and it must not be
   posted at the old window's coordinates (where another app now sits). */
TEST(app_switch_mid_hold_delivers_release) {
    Rig rig;
    window_rig(&rig);
    fake.cursor = (ScreenPoint){150, 650};                   // bottom-left of the game window
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);

    // Another app's window, somewhere else, is now the target
    fake_set_window(1000, 100, 600, 500, OTHER_WIN, OTHER_PID);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);          // delivered
    const FakeEvent *up = fake_last(FAKE_BUTTON);
    ASSERT_FALSE(up->pressed);
    ASSERT(!(up->position.x == 150 && up->position.y == 650));
    ASSERT_TRUE(every_post_inside_current_rect(&rig, fake.log_count - 1));

    // A fresh click afterwards lands in the new window, with no leftovers
    fake_clear_log();
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 0));

    // The window vanishes mid-hold (app quit): the release still goes out
    window_rig(&rig);
    rig_report(&rig, 0, 0, 1023, 0, 0, 0, 0);
    fake_clear_window();
    rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 500);
    TEST_PASS();
}

/* A second window of the same app, at the same spot, is still a different
   window: the release is not translated by the old window's origin. */
TEST(release_in_different_window_is_not_translated) {
    Rig rig;
    window_rig(&rig);
    press(&rig, MOUSE_BUTTON_LEFT);
    fake_set_window(300, 200, 800, 600, GAME_WIN + 1, GAME_PID);
    release(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 500);      // where the cursor is
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.y, 400);
    TEST_PASS();
}

TEST(double_click_counts) {
    Rig rig;
    window_rig(&rig);
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 1);
    release(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 1);

    fake.now_us += 200000;                                   // 200 ms later, same place
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 2);
    release(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 2);

    fake.now_us += DOUBLE_CLICK_INTERVAL_US + 1;             // too slow: a new single click
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 1);
    release(&rig, MOUSE_BUTTON_LEFT);

    fake.now_us += 100000;                                   // quick, but the cursor moved away
    fake.cursor = (ScreenPoint){560, 400};
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 1);
    release(&rig, MOUSE_BUTTON_LEFT);

    fake.now_us += 100000;                                   // other buttons count separately
    press(&rig, MOUSE_BUTTON_RIGHT);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->clicks, 1);
    TEST_PASS();
}

TEST(invalid_button_is_ignored) {
    Rig rig;
    window_rig(&rig);
    press(&rig, 3);
    press(&rig, -1);
    release(&rig, 99);
    ASSERT_EQ(fake.log_count, 0);
    TEST_PASS();
}

/*******************************************************************************
 * Regressions found on hardware (Minecraft, 2026-11-16), with the DEFAULT config
 ******************************************************************************/
#define RESIZE_BORDER 8.0    // A click this close to the frame can grab the resize handle

static void default_window_rig(Rig *rig) {
    rig_init(rig);
    rig->config.cursor.confine = CONFINE_WINDOW;     // everything else at its default
    fake_set_window(100, 100, 800, 600, GAME_WIN, GAME_PID);
    fake.cursor = (ScreenPoint){500, 400};
}

/* Reported: hold the look stick (the hidden cursor runs to the window edge),
   pull RT, and the window starts resizing. The click landed 2 pt inside the
   frame, on the resize border. With the default margin, a click after pinning
   against any edge or corner must land clear of the border, and so must the
   drag and the release that follow. */
TEST(click_after_pinning_at_edge_misses_resize_border) {
    const int directions[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                  {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (int d = 0; d < 8; d++) {
        Rig rig;
        default_window_rig(&rig);
        ScreenRect frame = fake.window.frame;
        rig_report(&rig, 0, 0, 0, 0, 0, directions[d][0] * 32767, -directions[d][1] * 32767);
        for (int i = 0; i < 300; i++) rig_tick(&rig, MOTION_TICK_MS);     // 3 s: pinned

        fake_clear_log();
        rig_report(&rig, 0, 0, 1023, 0, 0,                                // RT down, still looking
                   directions[d][0] * 32767, -directions[d][1] * 32767);
        for (int i = 0; i < 50; i++) rig_tick(&rig, MOTION_TICK_MS);
        rig_report(&rig, 0, 0, 0, 0, 0, 0, 0);                            // release

        ASSERT_EQ(fake.mouse_down[MOUSE_BUTTON_LEFT], 1);
        ASSERT_EQ(fake.mouse_up[MOUSE_BUTTON_LEFT], 1);
        for (int i = 0; i < fake.log_count; i++) {
            const FakeEvent *event = &fake.log[i];
            if (event->type != FAKE_BUTTON && event->type != FAKE_MOVE) continue;
            ASSERT(event->position.x >= frame.x + RESIZE_BORDER);
            ASSERT(event->position.x <= frame.x + frame.w - 1 - RESIZE_BORDER);
            ASSERT(event->position.y >= frame.y + RESIZE_BORDER);
            ASSERT(event->position.y <= frame.y + frame.h - 1 - RESIZE_BORDER);
        }
    }
    TEST_PASS();
}

/* Reported: looking around turns jittery in fullscreen but not in a window.
   In fullscreen the cursor was pinned on the display edge itself, where macOS
   reveals the menu bar / Dock. With the default config the cursor must stay
   clear of every display edge, while the deltas keep flowing. */
TEST(fullscreen_cursor_stays_off_display_edges) {
    const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int d = 0; d < 4; d++) {
        Rig rig;
        default_window_rig(&rig);
        fake_set_window(0, 0, 1920, 1080, GAME_WIN, GAME_PID);
        fake.window.fullscreen = true;
        rig_report(&rig, 0, 0, 0, 0, 0, directions[d][0] * 32767, -directions[d][1] * 32767);
        for (int i = 0; i < 600; i++) {
            rig_tick(&rig, MOTION_TICK_MS);
            const FakeEvent *event = fake_last(FAKE_MOVE);
            ASSERT_NOT_NULL(event);
            ASSERT(event->position.x >= RESIZE_BORDER && event->position.x <= 1919 - RESIZE_BORDER);
            ASSERT(event->position.y >= RESIZE_BORDER && event->position.y <= 1079 - RESIZE_BORDER);
            ASSERT(event->dx != 0 || event->dy != 0);
        }
        ASSERT_EQ(fake.moves, 600);              // one full-delta event per tick, no gaps
    }
    TEST_PASS();
}

/*******************************************************************************
 * Fallbacks and modes
 ******************************************************************************/

/* Desktop / no focused window: clamp to the displays, as before the overhaul. */
TEST(no_window_falls_back_to_displays) {
    Rig rig;
    window_rig(&rig);
    fake_clear_window();
    fake.cursor = (ScreenPoint){1915, 500};
    for (int i = 0; i < 5; i++) move(&rig, 4.0f, 2.0f);
    ASSERT_EQ(fake.cursor.x, 1919);              // stops at the display edge...
    ASSERT_EQ(fake.cursor.y, 510);               // ...while the free axis keeps moving
    ASSERT_EQ(fake_last(FAKE_MOVE)->dx, 4);      // deltas still complete

    // Clicks are not moved when there is no window to move them into
    fake.cursor = (ScreenPoint){1900, 20};
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 1900);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.y, 20);
    TEST_PASS();
}

/* The driver's own window (its About box) is never a confinement target. */
TEST(own_app_frontmost_is_ignored) {
    Rig rig;
    window_rig(&rig);
    fake_set_window(700, 300, 400, 200, 5, RIG_OWN_PID);
    fake.cursor = (ScreenPoint){200, 900};       // outside that window
    move(&rig, 10.0f, 0.0f);
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.x, 210);
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.y, 900);
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 210);
    TEST_PASS();
}

/* "display" mode with two displays that do not form a rectangle. */
TEST(display_mode_follows_display_shapes) {
    Rig rig;
    window_rig(&rig);
    rig.config.cursor.confine = CONFINE_DISPLAY;
    fake.displays[0] = (ScreenRect){0, 0, 1920, 1080};
    fake.displays[1] = (ScreenRect){-1280, 200, 1280, 720};   // left of main, lower
    fake.display_count = 2;

    // The window is ignored in this mode
    fake.cursor = (ScreenPoint){895, 400};
    move(&rig, 50.0f, 0.0f);
    ASSERT_EQ(fake.cursor.x, 945);

    // Crossing onto the left display works where the two touch...
    fake.cursor = (ScreenPoint){5, 500};
    move(&rig, -20.0f, 0.0f);
    ASSERT_EQ(fake.cursor.x, -15);
    // ...and is blocked above it, where there is no display
    fake.cursor = (ScreenPoint){5, 100};
    move(&rig, -20.0f, 6.0f);
    ASSERT_EQ(fake.cursor.x, 0);                 // right up to the edge, no further
    ASSERT_EQ(fake.cursor.y, 106);               // the free axis still moves
    ASSERT_EQ(fake_last(FAKE_MOVE)->dx, -20);

    // Far corner of the left display: reaches the last point exactly, even
    // with steps that do not divide the distance
    fake.cursor = (ScreenPoint){-1275, 915};
    for (int i = 0; i < 4; i++) move(&rig, -3.0f, 3.0f);
    ASSERT_EQ(fake.cursor.x, -1280);
    ASSERT_EQ(fake.cursor.y, 919);
    ASSERT_EQ(fake.moves, 7);

    // A large step into a corner still lands on a display
    fake.cursor = (ScreenPoint){1000, 500};
    move(&rig, 5000.0f, 5000.0f);
    ASSERT_EQ(fake.cursor.x, 1919);
    ASSERT_EQ(fake.cursor.y, 1079);
    TEST_PASS();
}

TEST(off_mode_does_not_clamp) {
    Rig rig;
    window_rig(&rig);
    rig.config.cursor.confine = CONFINE_OFF;
    fake.cursor = (ScreenPoint){1915, 500};
    move(&rig, 40.0f, 0.0f);
    ASSERT_EQ(fake_last(FAKE_MOVE)->position.x, 1955);       // left to the OS
    fake.cursor = (ScreenPoint){1500, 50};
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 1500);
    TEST_PASS();
}

/* A window hanging off the edge of the display: stay in the window AND on
   the display. */
TEST(window_partly_off_screen) {
    Rig rig;
    window_rig(&rig);
    fake_set_window(1600, 800, 800, 600, GAME_WIN, GAME_PID);   // right/bottom parts off-screen
    fake.cursor = (ScreenPoint){1900, 1000};
    ScreenRect rect;
    ASSERT_TRUE(current_rect(&rig, &rect));
    for (int i = 0; i < 50; i++) {
        move(&rig, 7.0f, 5.0f);
        const FakeEvent *event = fake_last(FAKE_MOVE);
        ASSERT_TRUE(rect_contains(rect, event->position));
        ASSERT_TRUE(platform_point_on_display(event->position));
        ASSERT_EQ(event->dx, 7);
    }
    ASSERT_EQ(fake.cursor.x, 1919);
    ASSERT_EQ(fake.cursor.y, 1079);
    TEST_PASS();
}

/* Streaming mode posts deltas and leaves the cursor where it is; clicks are
   still kept inside the window. */
TEST(streaming_mode_posts_deltas_only) {
    Rig rig;
    window_rig(&rig);
    rig.config.cursor.streaming_mode = true;
    for (int i = 0; i < 100; i++) {
        move(&rig, 12.0f, -3.0f);
        const FakeEvent *event = fake_last(FAKE_MOVE);
        ASSERT_EQ(event->position.x, 500);
        ASSERT_EQ(event->position.y, 400);
        ASSERT_EQ(event->dx, 12);
        ASSERT_EQ(event->dy, -3);
    }
    ASSERT_EQ(fake.warps, 0);

    fake.cursor = (ScreenPoint){1500, 400};
    press(&rig, MOUSE_BUTTON_LEFT);
    ASSERT_EQ(fake.warps, 1);
    ASSERT_EQ(fake_last(FAKE_BUTTON)->position.x, 897);
    TEST_PASS();
}

/* advanced.recenter_when_captured: at an edge, jump back to the middle of the
   window instead of pressing against the edge. Deltas are unaffected. */
TEST(recenter_option) {
    Rig rig;
    window_rig(&rig);
    rig.config.cursor.recenter_when_captured = true;
    fake.cursor = (ScreenPoint){600, 400};
    move(&rig, 10.0f, 0.0f);                     // not at an edge: plain move
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake.cursor.x, 610);

    fake.cursor = (ScreenPoint){895, 400};
    move(&rig, 10.0f, 0.0f);                     // would hit the right edge
    ASSERT_EQ(fake.warps, 1);
    const FakeEvent *event = fake_last(FAKE_MOVE);
    ASSERT_EQ(event->position.x, 500);           // center of 102..897
    ASSERT_EQ(event->position.y, 414);           // center of 130..697
    ASSERT_EQ(event->dx, 10);
    ASSERT_TRUE(every_post_inside_current_rect(&rig, 0));

    // Off by default: the same move just stops at the edge
    window_rig(&rig);
    fake.cursor = (ScreenPoint){895, 400};
    move(&rig, 10.0f, 0.0f);
    ASSERT_EQ(fake.warps, 0);
    ASSERT_EQ(fake.cursor.x, 897);
    TEST_PASS();
}

/* cursor_dispatch posts a mixed queue in order. */
TEST(dispatch_preserves_order) {
    Rig rig;
    window_rig(&rig);
    OutQueue queue;
    out_queue_init(&queue);
    queue.events[queue.count++] = (OutEvent){.type = OUT_KEY, .code = KEY_W, .pressed = true};
    queue.events[queue.count++] = (OutEvent){.type = OUT_MOUSE_BUTTON, .code = MOUSE_BUTTON_RIGHT,
                                             .pressed = true};
    queue.events[queue.count++] = (OutEvent){.type = OUT_MOUSE_MOVE, .dx = 3.0f, .dy = 0.0f,
                                             .drag_button = MOUSE_BUTTON_RIGHT};
    queue.events[queue.count++] = (OutEvent){.type = OUT_SCROLL, .lines = -1};
    queue.events[queue.count++] = (OutEvent){.type = OUT_KEY, .code = KEY_W, .pressed = false};
    cursor_dispatch(&rig.cursor, &rig.config.cursor, &queue);

    ASSERT_EQ(fake.log_count, 5);
    ASSERT_EQ(fake.log[0].type, FAKE_KEY);
    ASSERT_TRUE(fake.log[0].pressed);
    ASSERT_EQ(fake.log[1].type, FAKE_BUTTON);
    ASSERT_EQ(fake.log[2].type, FAKE_MOVE);
    ASSERT_EQ(fake.log[2].drag_button, MOUSE_BUTTON_RIGHT);
    ASSERT_EQ(fake.log[3].type, FAKE_SCROLL);
    ASSERT_EQ(fake.log[3].lines, -1);
    ASSERT_EQ(fake.log[4].type, FAKE_KEY);
    ASSERT_FALSE(fake.log[4].pressed);
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Cursor Confinement");

    printf("\n-- confine() geometry --\n");
    RUN_TEST(confine_inside_moves_freely);
    RUN_TEST(confine_each_edge_keeps_other_axis);
    RUN_TEST(confine_past_each_corner);
    RUN_TEST(confine_delta_larger_than_rect);
    RUN_TEST(confine_start_outside_is_pulled_in);
    RUN_TEST(confine_negative_coordinates);

    printf("\n-- Window rect --\n");
    RUN_TEST(rect_is_frame_inset_by_margin_and_title_bar);
    RUN_TEST(rect_fullscreen_keeps_margin_drops_title_inset);
    RUN_TEST(rect_unusable_windows_are_rejected);

    printf("\n-- Motion --\n");
    RUN_TEST(move_inside_window);
    RUN_TEST(delta_fields_always_carry_requested_motion);
    RUN_TEST(move_slides_along_edge);
    RUN_TEST(subpixel_remainder_survives_clamping);
    RUN_TEST(move_from_outside_enters_window);
    RUN_TEST(real_mouse_is_never_fought);
    RUN_TEST(captured_cursor_keeps_turning);

    printf("\n-- Moving and resizing windows --\n");
    RUN_TEST(resize_and_move_sequence_stays_confined);
    RUN_TEST(live_resize_every_tick_stays_confined);
    RUN_TEST(stale_rect_is_never_used);

    printf("\n-- Clicks --\n");
    RUN_TEST(click_outside_window_is_moved_inside);
    RUN_TEST(right_and_middle_clicks_are_confined_too);
    RUN_TEST(click_inside_window_does_not_warp);
    RUN_TEST(trigger_click_from_outside_end_to_end);
    RUN_TEST(drag_stays_confined);
    RUN_TEST(release_follows_moved_window);
    RUN_TEST(app_switch_mid_hold_delivers_release);
    RUN_TEST(release_in_different_window_is_not_translated);
    RUN_TEST(double_click_counts);
    RUN_TEST(invalid_button_is_ignored);

    printf("\n-- Hardware regressions --\n");
    RUN_TEST(click_after_pinning_at_edge_misses_resize_border);
    RUN_TEST(fullscreen_cursor_stays_off_display_edges);

    printf("\n-- Fallbacks and modes --\n");
    RUN_TEST(no_window_falls_back_to_displays);
    RUN_TEST(own_app_frontmost_is_ignored);
    RUN_TEST(display_mode_follows_display_shapes);
    RUN_TEST(off_mode_does_not_clamp);
    RUN_TEST(window_partly_off_screen);
    RUN_TEST(streaming_mode_posts_deltas_only);
    RUN_TEST(recenter_option);
    RUN_TEST(dispatch_preserves_order);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
