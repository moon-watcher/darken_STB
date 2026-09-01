# Darken — Option 2: Entity-Owned State

This version changes the callback model to more closely resemble the classic Doom/idTech thinker pattern.

The callback receives the complete entity and modifies its own state directly.

```c
typedef void (*darken_state)(darken_entity);
```

There are no special return values.

Example:

```c
void enemy_enter(darken_entity entity)
{
    Enemy *e = (Enemy *)entity->data;

    e->y++;

    if (e->y >= 40)
        entity->update = enemy_attack;
}
```

The update loop simply executes the current callback:

```c
if (entity->update)
    entity->update(entity);
```

## State transitions

A state transition is explicit:

```c
entity->update = enemy_attack;
```

## Lifecycle

Lifecycle operations are explicit API calls:

```c
darken_entity_pause(entity);
darken_entity_resume(entity);
darken_entity_delete(entity);
```

Example:

```c
void enemy_update(darken_entity entity)
{
    Enemy *e = (Enemy *)entity->data;

    if (e->y > SCREEN_HEIGHT)
    {
        darken_entity_delete(entity);
        return;
    }

    e->y += e->vy;
}
```

## Advantages

- Clear separation between state and lifecycle.
- No magic return values.
- The callback has access to the complete entity.
- `entity->update = next_state` is explicit.
- Closely matches the classic thinker/state-machine style.

## Trade-offs

- Callbacks are coupled to `darken_entity`.
- `DARKEN_DATA_GET_ENTITY()` becomes less important.
- Lifecycle operations can happen inside callbacks, so the update loop must be designed carefully.
