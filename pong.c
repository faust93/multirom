#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#include "framebuffer.h"
#include "input.h"
#include "pong.h"
#include "util.h"

#define SCORE_SPACE 75
#define L 0
#define R 1

#define PADDLE_W 150
#define PADDLE_H 60
#define PADDLE_REF ((PADDLE_H/3)*2)
#define PADDLE_Y 20

#define BALL_W 25
#define DEFAULT_BALL_SPEED 10
#define BALL_SPEED_MOD ((PADDLE_W/2)/ball_speed)

#define COMPUTER L
#define COMPUTER_SPEED 10

static fb_text *score[2] = { NULL, NULL };
static fb_rect *paddles[2] = { NULL, NULL };
static fb_rect *ball = NULL;
static int paddle_last_x[2] = { -1, -1 };
static int paddle_touch_id[2] = { -1, -1 };
static int ball_speed = DEFAULT_BALL_SPEED;
static int enable_computer = 1;

typedef struct
{
    int x;
    int y;
    int collision;
} ball_step;

enum
{
    COL_NONE    = 0,
    COL_LEFT    = 1,
    COL_RIGHT   = 2,
    COL_TOP     = 3,
    COL_BOTTOM  = 4,
};

static ball_step **movement_steps = NULL;
static float ball_speed_x = 0;
static float ball_speed_y = 0;

static float ai_last_speed = -1000;
static int ai_hit_pos = 0;

void pong(void)
{
    enable_computer = 1;
    paddle_touch_id[L] = -1;
    paddle_touch_id[R] = -1;

    // middle line
    fb_add_rect(0, fb_height/2 - 1, fb_width, 1, WHITE);

    score[L] = fb_add_text(0, fb_height/2 - SIZE_EXTRA*16 - 20, WHITE, SIZE_EXTRA, "0");
    score[R] = fb_add_text(0, fb_height/2 + 20, WHITE, SIZE_EXTRA, "0");

    paddles[L] = fb_add_rect(100, PADDLE_Y, PADDLE_W, PADDLE_H, WHITE);
    paddles[R] = fb_add_rect(100, fb_height-PADDLE_Y-PADDLE_H, PADDLE_W, PADDLE_H, WHITE);

    ball = fb_add_rect(0, 0, BALL_W, BALL_W, WHITE);

    pong_spawn_ball(rand()%2);
    pong_calc_movement();

    add_touch_handler(&pong_touch_handler, NULL);

    int step = 0;
    volatile int run = 1;
    while(run)
    {
        switch(get_last_key())
        {
            case KEY_POWER:
                run = 0;
                break;
            case KEY_VOLUMEUP:
                ball_speed += 5;
                pong_spawn_ball(rand()%2);
                pong_calc_movement();
                step = 0;
                break;
            case KEY_VOLUMEDOWN:
                if(ball_speed > 5)
                    ball_speed -= 5;
                pong_spawn_ball(rand()%2);
                pong_calc_movement();
                step = 0;
                break;
        }

        step = pong_do_movement(step);

        fb_draw();
        usleep(1000);
    }

    rm_touch_handler(&pong_touch_handler, NULL);

    list_clear(&movement_steps, &free);
}

int pong_do_movement(int step)
{
    if(!movement_steps[step])
    {
        pong_calc_movement();
        return 0;
    }

    int col = movement_steps[step]->collision;
    if(col == COL_NONE || col == COL_LEFT || col == COL_RIGHT)
    {
        ball->head.x = movement_steps[step]->x;
        ball->head.y = movement_steps[step]->y;
        if(enable_computer)
            pong_handle_ai();
    }

    switch(col)
    {
        case COL_NONE:
            return step+1;
        case COL_TOP:
        case COL_BOTTOM:
            ball_speed_x = -ball_speed_x;
            break;
        case COL_LEFT:
        case COL_RIGHT:
        {
            int s = col - 1;
            if(ball->head.x+BALL_W >= paddles[s]->head.x && ball->head.x <= paddles[s]->head.x+PADDLE_W)
            {
                // Increase X speed according to distance from center of paddle.
                ball_speed_x = (float)((ball->head.x + BALL_W/2) - (paddles[s]->head.x + PADDLE_W/2))/BALL_SPEED_MOD;
                ball_speed_y = -ball_speed_y;
            }
            else
            {
                pong_add_score(!s);
                for(col = 0; col < 1000; col += 15)
                {
                    fb_draw();
                    usleep(15000);
                }
                pong_spawn_ball(s);
            }
            break;
        }
    }

    pong_calc_movement();
    return 0;
}

int pong_touch_handler(touch_event *ev, void *data)
{
    int i = 0;
    for(; i < 2; ++i)
    {
        if (paddle_touch_id[i] == -1 && (ev->changed & TCHNG_ADDED) &&
            in_rect(ev->x, ev->y, paddles[i]->head.x, paddles[i]->head.y, paddles[i]->w, paddles[i]->h))
        {
            paddle_touch_id[i] = ev->id;
            paddle_last_x[i] = ev->x;
            if(i == L)
                enable_computer = 0;
            return 0;
        }

        if(ev->id != paddle_touch_id[i])
            continue;

        if(ev->changed & TCHNG_REMOVED)
        {
            paddle_touch_id[i] = -1;
            return 0;
        }

        int newX = paddles[i]->head.x + (ev->x - paddle_last_x[i]);
        paddle_last_x[i] = ev->x;

        if(newX > 0 && newX < fb_width-PADDLE_W)
            paddles[i]->head.x = newX;
        return 0;
    }
    return -1;
}

void pong_spawn_ball(int side)
{
    float angle;
    if(side == L)
        angle = 6.28319 - (float)(rand()%1570)/1000.f - 0.785398;
    else
        angle = (float)(rand()%1570)/1000.f + 0.785398;

    ball_speed_x = cos(angle)*ball_speed;
    ball_speed_y = sin(angle)*ball_speed;

    ball->head.x = rand()%(fb_width-BALL_W);
    ball->head.y = fb_height/2 - BALL_W/2;
}

void pong_calc_movement(void)
{
    list_clear(&movement_steps, &free);

    ball_step *step = NULL;
    float x = ball->head.x;
    float y = ball->head.y;

    if(y < PADDLE_Y+PADDLE_H)
        y = PADDLE_Y+PADDLE_H;
    else if(y > fb_height-PADDLE_Y-PADDLE_H-BALL_W)
        y = fb_height-PADDLE_Y-PADDLE_H-BALL_W;

    if(x < 0)
        x = 0;
    else if(x > fb_width-BALL_W)
        x = fb_width-BALL_W;

    while(!step || step->collision == COL_NONE)
    {
        x += ball_speed_x;
        y += ball_speed_y;

        step = malloc(sizeof(ball_step));
        step->x = x;
        step->y = y;
        step->collision = pong_get_collision(x, y);
        list_add(step, &movement_steps);
    }
}

int pong_get_collision(int x, int y)
{
    if(y < PADDLE_Y+PADDLE_REF)
        return COL_LEFT;
    if(y > fb_height-PADDLE_Y-PADDLE_REF-BALL_W)
        return COL_RIGHT;

    if(x < 0)
        return COL_BOTTOM;
    if(x > fb_width-BALL_W)
        return COL_TOP;

    return COL_NONE;
}

void pong_add_score(int side)
{
    char buff[16];
    int curr = atoi(score[side]->text);

    sprintf(buff, "%d", ++curr);
    score[side]->text = realloc(score[side]->text, strlen(buff)+1);
    strcpy(score[side]->text, buff);
}

void pong_handle_ai(void)
{
    int ball_center = (ball->head.x + BALL_W/2);

    if(ai_last_speed != ball_speed_x)
    {
        ai_hit_pos = rand()%3;
        ai_last_speed = ball_speed_x;
    }

    int computer_x = 0;
    switch(ai_hit_pos)
    {
        case 0:
            computer_x = paddles[COMPUTER]->head.x + PADDLE_W/2;
            break;
        case 1:
            computer_x = paddles[COMPUTER]->head.x;
            break;
        case 2:
            computer_x = paddles[COMPUTER]->head.x + PADDLE_W;
            break;
    }

    int move_dist = abs(computer_x - ball_center);
    if(move_dist > COMPUTER_SPEED)
        move_dist = COMPUTER_SPEED;

    if(ball_center > computer_x)
    {
        if(paddles[COMPUTER]->head.x + PADDLE_W + COMPUTER_SPEED <= fb_width)
            paddles[COMPUTER]->head.x += move_dist;
    }
    else
    {
        if(paddles[COMPUTER]->head.x - COMPUTER_SPEED >= 0)
            paddles[COMPUTER]->head.x -= move_dist;
    }
}
