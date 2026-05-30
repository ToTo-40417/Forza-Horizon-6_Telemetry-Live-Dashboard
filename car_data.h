#pragma once

// CarOrdinal lookup table.
//
// Seed data is based on the official Forza Horizon 5 Cars List ID column.
// FH6 appears to expose the same style of CarOrdinal value in Data Out, but a
// complete official FH6 ordinal-to-name table is not currently published.
// Unknown FH6 IDs intentionally fall back to "#<ordinal>" in the dashboard.

struct CarDataEntry {
  int32_t ordinal;
  const char* name;
  const char* type;
};

static const CarDataEntry CAR_DATA[] = {
  {2352, "2017 Acura NSX", "Modern Supercars"},
  {2968, "2023 Aston Martin Valkyrie", "Hypercars"},
  {3631, "2022 Aston Martin Valkyrie AMR Pro", "Extreme Track Toys"},
  {3364, "2019 Aston Martin Valhalla Concept Car", "Hypercars"},
  {3211, "2017 Aston Martin Vulcan AMR Pro", "Extreme Track Toys"},
  {1181, "2010 Aston Martin One-77", "Super GT"},
  {3168, "2019 Bugatti Divo", "Extreme Track Toys"},
  {2624, "2018 Bugatti Chiron", "Hypercars"},
  {1328, "2011 Bugatti Veyron Super Sport", "Hypercars"},
  {1219, "1992 Bugatti EB110 Super Sport", "Retro Supercars"},
  {3771, "2024 Chevrolet Corvette E-Ray", "Modern Supercars"},
  {3766, "2023 Chevrolet Corvette Z06", "Track Toys"},
  {3369, "2020 Chevrolet Corvette Stingray Coupe", "Modern Supercars"},
  {3118, "2019 Chevrolet Corvette ZR1", "Track Toys"},
  {3724, "2022 Ferrari 296 GTB", "Modern Supercars"},
  {3595, "2020 Ferrari SF90 Stradale", "Hypercars"},
  {3311, "2018 Ferrari FXX-K EVO", "Extreme Track Toys"},
  {2974, "2017 Ferrari 812 Superfast", "Super GT"},
  {2034, "2013 Ferrari LaFerrari", "Hypercars"},
  {333, "2002 Ferrari Enzo Ferrari", "Retro Supercars"},
  {1023, "1989 Ferrari F40 Competizione", "Extreme Track Toys"},
  {340, "1987 Ferrari F40", "Retro Supercars"},
  {3315, "2020 Koenigsegg Jesko", "Hypercars"},
  {2910, "2017 Koenigsegg Agera RS", "Hypercars"},
  {2526, "2016 Koenigsegg Regera", "Hypercars"},
  {2188, "2015 Koenigsegg One:1", "Hypercars"},
  {3891, "2024 Lamborghini Revuelto", "Hypercars"},
  {3775, "2021 Lamborghini Aventador LP 780-4 Ultimae", "Hypercars"},
  {3774, "2021 Lamborghini Countach LPI 800-4", "Hypercars"},
  {3606, "2020 Lamborghini Essenza SCV12", "Extreme Track Toys"},
  {3289, "2018 Lamborghini Aventador SVJ", "Hypercars"},
  {2616, "2016 Lamborghini Centenario LP 770-4", "Hypercars"},
  {2042, "2013 Lamborghini Veneno", "Hypercars"},
  {1322, "2011 Lamborghini Sesto Elemento", "Extreme Track Toys"},
  {2941, "2011 Lamborghini Sesto Elemento Forza Edition", "Extreme Track Toys"},
  {3668, "2023 McLaren Artura", "Modern Supercars"},
  {3482, "2021 McLaren 765LT", "Track Toys"},
  {3700, "2021 McLaren Sabre", "Hypercars"},
  {3156, "2019 McLaren Speedtail", "Hypercars"},
  {2988, "2018 McLaren Senna", "Hypercars"},
  {1667, "2013 McLaren P1", "Hypercars"},
  {1314, "1993 McLaren F1", "Retro Supercars"},
  {3921, "2024 Nissan Z NISMO", "Modern Sports Cars"},
  {3620, "2023 Nissan Z", "Modern Sports Cars"},
  {3543, "2021 Pagani Huayra R", "Extreme Track Toys"},
  {2647, "2016 Pagani Huayra BC", "Hypercars"},
  {1175, "2010 Pagani Zonda R", "Extreme Track Toys"},
  {3625, "2021 Rimac Nevera", "Hypercars"},
  {3140, "2019 Rimac Concept Two", "Hypercars"},
  {3781, "2023 Porsche 911 GT3 RS", "Track Toys"},
  {3698, "2022 Porsche Mission R", "Extreme Track Toys"},
  {3445, "2020 Porsche Taycan Turbo S", "Super Saloons"},
  {2290, "2014 Porsche 918 Spyder", "Hypercars"},
  {292, "2003 Porsche Carrera GT", "Retro Supercars"},
  {3402, "2020 Toyota GR Supra", "Modern Sports Cars"},
};

static const size_t CAR_DATA_COUNT = sizeof(CAR_DATA) / sizeof(CAR_DATA[0]);

const CarDataEntry* findCarData(int32_t ordinal) {
  for (size_t i = 0; i < CAR_DATA_COUNT; i++) {
    if (CAR_DATA[i].ordinal == ordinal) return &CAR_DATA[i];
  }
  return nullptr;
}
