# Darken — Callback Model Comparison

Darken can implement its state machine in two closely related ways.

## Option 1 — Return the Next State

```c
typedef void *(*darken_state)();
```

The callback receives the payload:

```c
darken_state enemy_enter(void *data)
{
    Enemy *e = data;

    if (e->y >= 40)
        return enemy_attack;

    return DARKEN_LOOP;
}
```

Special return values control lifecycle:

```c
return DARKEN_LOOP;
return DARKEN_PAUSE;
return DARKEN_DELETE;
```

Conceptually:

```text
callback(data)
      |
      +---- return next callback
      |
      +---- return LOOP / PAUSE / DELETE
      |
      v
    Darken
```

**The callback describes what should happen next, and Darken applies the transition.**

## Option 2 — Entity Owns Its State

```c
typedef void (*darken_state)(darken_entity);
```

The callback receives the entity:

```c
void enemy_enter(darken_entity entity)
{
    Enemy *e = (Enemy *)entity->data;

    if (e->y >= 40)
        entity->update = enemy_attack;
}
```

Lifecycle is explicit:

```c
darken_entity_pause(entity);
darken_entity_delete(entity);
```

Conceptually:

```text
callback(entity)
      |
      +---- entity->update = next callback
      |
      +---- darken_entity_delete(entity)
      |
      v
  entity modifies itself
```

**The entity owns its current state and lifecycle.**

## Shmup Example

### Option 1

```c
darken_state bullet_update(void *data)
{
    Bullet *b = data;

    b->y -= 6;

    if (b->y < 0)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

### Option 2

```c
void bullet_update(darken_entity entity)
{
    Bullet *b = (Bullet *)entity->data;

    b->y -= 6;

    if (b->y < 0)
        darken_entity_delete(entity);
}
```

## Key Difference

| | Option 1 | Option 2 |
|---|---|---|
| Callback receives | `data` | `darken_entity` |
| State transition | `return next_state` | `entity->update = next_state` |
| Continue | `DARKEN_LOOP` | Do nothing |
| Pause | `return DARKEN_PAUSE` | `darken_entity_pause()` |
| Delete | `return DARKEN_DELETE` | `darken_entity_delete()` |
| Magic values | Yes | No |
| Callback knows entity | No | Yes |
| Similar to Doom thinker style | Partially | More closely |

Both retain Darken's core architecture: fixed-capacity pools, stable entity addresses, pointer swapping, and O(1) lifecycle operations.
