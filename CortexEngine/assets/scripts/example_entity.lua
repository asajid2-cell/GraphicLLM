-- example_entity.lua
-- Example script demonstrating CortexEngine Lua scripting.

local ExampleEntity = {}

-- Properties (can be edited in inspector)
ExampleEntity.moveSpeed = 5.0
ExampleEntity.rotateSpeed = 90.0
ExampleEntity.targetPosition = {x = 0, y = 0, z = 0}
ExampleEntity.health = 100
ExampleEntity.maxHealth = 100

-- Private state
local startPosition = nil
local elapsedTime = 0

-- Called when the entity is created
function ExampleEntity:OnStart()
    print("ExampleEntity started on entity " .. tostring(self.entity))

    -- Store starting position
    startPosition = Transform.GetPosition(self.entity)

    -- Find another entity by name
    local target = Entity.FindByName("Target")
    if target then
        self.targetPosition = Transform.GetPosition(target)
    end
end

-- Called every frame
function ExampleEntity:OnUpdate(deltaTime)
    elapsedTime = elapsedTime + deltaTime

    -- Get current position
    local pos = Transform.GetPosition(self.entity)

    -- Move towards target
    local dir = Vec3.Sub(self.targetPosition, pos)
    local distance = Vec3.Length(dir)

    if distance > 0.1 then
        local moveDir = Vec3.Normalized(dir)
        local moveAmount = math.min(self.moveSpeed * deltaTime, distance)
        local newPos = Vec3.Add(pos, Vec3.Mul(moveDir, moveAmount))
        Transform.SetPosition(self.entity, newPos)

        -- Rotate to face movement direction
        Transform.LookAt(self.entity, self.targetPosition)
    end

    -- Bob up and down
    pos = Transform.GetPosition(self.entity)
    pos.y = pos.y + math.sin(elapsedTime * 2) * 0.01
    Transform.SetPosition(self.entity, pos)
end

-- Called at fixed intervals for physics
function ExampleEntity:OnFixedUpdate(fixedDeltaTime)
    -- Physics-related logic here
end

-- Called when destroyed
function ExampleEntity:OnDestroy()
    print("ExampleEntity destroyed")
end

-- Called when colliding with another entity
function ExampleEntity:OnCollisionEnter(other)
    local otherName = Entity.GetName(other)
    print("Collision with: " .. otherName)

    -- Example: take damage from enemy
    if Entity.GetTag(other) == "Enemy" then
        self:TakeDamage(10)
    end
end

-- Custom method: Take damage
function ExampleEntity:TakeDamage(amount)
    self.health = self.health - amount

    if self.health <= 0 then
        self.health = 0
        self:Die()
    end

    -- Visual feedback
    Debug.Log("Health: " .. tostring(self.health) .. "/" .. tostring(self.maxHealth))
end

-- Custom method: Heal
function ExampleEntity:Heal(amount)
    self.health = math.min(self.health + amount, self.maxHealth)
end

-- Custom method: Die
function ExampleEntity:Die()
    Debug.Log("Entity died!")

    -- Spawn particle effect
    -- Scene.Instantiate("effects/explosion", Transform.GetPosition(self.entity))

    -- Destroy after delay
    Scene.Destroy(self.entity, 0.5)
end

-- Custom method: Set target
function ExampleEntity:SetTarget(targetEntity)
    if Entity.IsValid(targetEntity) then
        self.targetPosition = Transform.GetPosition(targetEntity)
    end
end

return ExampleEntity
