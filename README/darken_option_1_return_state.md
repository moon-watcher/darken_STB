# Darken — Option 1: Return Next State

This version keeps Darken's current callback model.

The update callback receives `entity->data` and returns either the next state callback or a lifecycle control value.

```c
typedef void *(*darken_state)();

#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP   ((void *)1)
#define DARKEN_PAUSE  ((void *)2)
```

Example:

```c
darken_state enemy_enter(void *data)
{
    Enemy *e = data;

    e->y++;

    if (e->y >= 40)
        return enemy_attack;

    return DARKEN_LOOP;
}
```

Darken installs the returned callback.

## Lifecycle

```c
return DARKEN_PAUSE;
return DARKEN_DELETE;
```

The callback does not directly manipulate the entity manager.

## Advantages

- Very compact state-machine syntax.
- Explicit transitions through return values.
- Callbacks work directly with the payload.
- Lifecycle control is handled centrally by Darken.

## Trade-offs

- The return value has two meanings: next state or lifecycle command.
- `DARKEN_LOOP`, `DARKEN_PAUSE` and `DARKEN_DELETE` are sentinel values.
- A callback cannot directly change `entity->update`.
- Less similar to Doom's classic thinker pattern.
