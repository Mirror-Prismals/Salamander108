Place the title logo PNG at:

`Procedures/assets/title_logo.png`

The menu title logo renderer reads this path from the `TitleLogo` instance in
`Entities/Worlds/menu_button_world.json`. Dark pixels are masked out at render
time with a lightness threshold, so a black-background logo PNG is fine.
