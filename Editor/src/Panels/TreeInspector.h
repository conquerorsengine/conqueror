#pragma once

#include "Scene/Entity.h"

namespace Conqueror::Editor
{
    // Tree bileşeninin alt kısmı (ComponentRegistry ExtraInspector): Unity Tree Creator gibi grup hiyerarşisi (gövde,
    // dallar, yapraklar), grup ekleme / çoğaltma / silme ve seçili grubun Distribution / Geometry / Shape ayarları
    void DrawTreeInspector(Entity entity);
}
