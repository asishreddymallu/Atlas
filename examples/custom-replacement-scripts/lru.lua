-- LRU in Lua: the same policy as the built-in LRU, for checking the custom-policy path.
-- Prefer an invalid line; otherwise evict the line used longest ago (smallest lastAccess).

function chooseVictim(lines, request, cache)
    for i, line in ipairs(lines) do
        if not line.valid then
            return i
        end
    end

    local victim = 1
    for i = 2, #lines do
        if lines[i].lastAccess < lines[victim].lastAccess then
            victim = i
        end
    end
    return victim
end
