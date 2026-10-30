# ESPECIFICACIÓN TÉCNICA Y MODELO MATEMÁTICO DE INGENIERÍA (REVISIÓN C)
**Proyecto:** Sistema de Telemetría, Auditoría Energética y Analítica Predictiva en el Borde (*Edge Computing*) para Salas de Climatización Crítica (Data Centers / Nodos Telco)  
**Plataforma de Hardware:** Controlador Industrial BrightC V3 (Arquitectura ARM Cortex-M7 STM32H7)  
**Estado del Documento:** Revisión C — Revisión B verificada contra el hardware BrightC V3 y el firmware base, con las correcciones C-01 a C-05. Sustituye a la Revisión B; el nombre del archivo se conserva por estabilidad de enlaces.

### Registro de cambios (B → C)

| ID | Sección | Cambio |
| --- | --- | --- |
| C-01 | 2.2 | Se documenta la cadena de ganancia del front-end RTD: el **INA826** con $R_g = 619\,\Omega$ entrega $G = 80.806$ y la etapa siguiente ($R_{24}/R_{26} = 10\,\text{k}\Omega/10\,\text{k}\Omega$) divide por 2, de modo que el ADC ve $G_{efectiva} = 40.403$. Se confirma el valor de la Revisión B y se deja explícito el divisor. |
| C-02 | 2.3 | Se confirma que la placa BrightC V3 incorpora la electrónica de acondicionamiento y aislamiento para $24\text{–}48\text{ V DC}$ antes del pin del expansor `MCP23S17`; los contactos secos se conectan directamente a las entradas designadas. |
| C-03 | 2.4 | El transductor de humedad se especifica como sensor digital $\text{I}^2\text{C}$ genérico; el modelo exacto se definirá en la ingeniería de detalle. |
| C-04 | 2.2, 2.4 | Se añade el procedimiento de puesta en servicio: los módulos se programan (dirección y baudios) **antes** de conectarse al bus, evitando la colisión de las direcciones de fábrica. |
| C-05 | 6.4 | Se deja constancia del requisito de plataforma: la repartición MCUboot asimétrica ya definida en la arquitectura OTA del proyecto. El DTS no se modifica en esta revisión. |

---

## 1. ALCANCE Y FILOSOFÍA DE OPERACIÓN

El sistema **BrightC V3** opera como una unidad de auditoría energética y procesamiento analítico local (*Edge Processing*) que monitorea en tiempo real el desempeño termodinámico, eléctrico y mecánico de sistemas de aire acondicionado en salas críticas de telecomunicaciones (arreglos redundantes $2+1$, $3+1$, etc.).

El controlador actúa como un **observador pasivo en paralelo** (sin interferir con el lazo de control de los termostatos existentes en el sitio), adquiriendo variables físicas directas para calcular balances de masa y energía, aislar transitorios electromecánicos, aprender la firma térmica de la sala y emitir diagnósticos predictivos hacia plataformas de gestión superior mediante **Modbus TCP Server** (Huawei NetEco), **SNMP** y una **API RESTful Local (HTTP/JSON)**, además de su visualización en pantalla táctil local (**HMI DWIN 4.3"**).

---

## 2. ARQUITECTURA CONFIGURABLE DE HARDWARE Y SENSÓRICA

El diseño del sistema es **100% paramétrico y dirigido por configuración**. Permite definir dinámicamente la cantidad de pares de sensores de temperatura, sensores de ambiente de sala, unidades manejadoras de aire (HVAC) y compresores por unidad.

### 2.1. Topología Matricial de Temperatura: Esquema $(N + N) + M$
La adquisición de temperatura se estructura bajo una arquitectura de **pares térmicos apareados ($N + N$)** más una red de **monitoreo ambiental de sala ($M$)**:

1. **Pares de Evaluación Termodinámica ($N$ Inyección + $N$ Retorno):**
   * Los puntos de medición sobre el flujo de climatización se instalan **siempre en pares ($1+1$, $2+2$, $\dots$, $N+N$)**.
   * Cada par $k \in \{1 \dots N\}$ está compuesto por una sonda de **Inyección de Aire Frío ($T_{inj,k}$)** y su correspondiente sonda de **Retorno de Aire Caliente ($T_{ret,k}$)**.
   * Dependiendo de la configuración del sitio, los $N$ pares pueden asignarse de forma dedicada a cada manejadora de aire o circuito (por ejemplo, 2 pares por cada una de las 3 unidades HVAC, o 1 par por circuito evaporador) o distribuirse como una matriz de muestreo espacial a lo largo de los plenos/ductos de inyección y rejillas de retorno.
2. **Sensores de Temperatura Ambiente de Sala ($M$ Sensores):**
   * De forma independiente a los pares de ducto/equipo, el sistema incorpora $M$ sensores adicionales ($T_{sala,m}$, con $m \in \{1 \dots M\}$) distribuidos en los pasillos o zonas críticas de racks para auditar la temperatura real del recinto, detectar puntos calientes (*hotspots*) y evaluar la efectividad del barrido térmico en la sala.
3. **Dimensionamiento para el Primer Sitio Piloto ($6 + 6 + 2 = 14\text{ Sondas PT100}$):**
   * **$N = 6$ Sondas de Aire Frío (Inyección):** $T_{inj,1} \dots T_{inj,6}$.
   * **$N = 6$ Sondas de Aire Caliente (Retorno):** $T_{ret,1} \dots T_{ret,6}$.
   * **$M = 2$ Sondas de Temperatura de Sala (Ambiente):** $T_{sala,1}, T_{sala,2}$.
   * **Total Piloto:** **14 sensores RTD PT100 Clase A** de 3 hilos con malla apantallada.

### 2.2. Canales de Adquisición de Temperatura (Interno ADC vs. Externo Modbus RS-485)
Para cubrir desde configuraciones pequeñas ($1+1+1$) hasta despliegues densos como el piloto de 14 sensores ($6+6+2$) o superiores, el sistema soporta dos vías de adquisición combinables por configuración:

1. **Módulo de Adquisición Digital RS-485 (`CWT-TM-16PT` — Vía Principal para el Piloto $6+6+2$):**
   * Concentrador industrial de **16 canales PT100** con compensación nativa de 3 hilos en bornera, conectado al bus RS-485 del BrightC V3 mediante protocolo **Modbus RTU**.
   * Permite conectar las **14 sondas PT100 del piloto** en un único módulo (`CH1` a `CH6` para Inyección, `CH7` a `CH12` para Retorno y `CH13` a `CH14` para Ambiente de Sala), dejando 2 canales libres de reserva en el módulo (más los 5 canales internos de la placa BrightC V3) y entregando todas las lecturas en una sola transacción Modbus con resolución de $0.1\,^\circ\text{C}$ o punto flotante IEEE 754.
2. **Etapa Analógica Interna en Placa BrightC V3 (Hasta 5 canales RTD con `INA826`):**
   * Disponible para sitios compactos o como canales suplementarios. Basada en puente de Wheatstone a $3.3\text{ V DC}$ ($R_{sup} = 5.1\text{ k}\Omega$ al $0.1\%$, referencia fija de $91\,\Omega$), amplificador de instrumentación **INA826** ($G_{efectiva} = 40.403$) y lectura por el ADC de 16 bits del STM32H7.
   * **Cadena de ganancia verificada (C-01):** el INA826 opera con $R_g = 619\,\Omega$ entre sus pines 1 y 8, lo que da $G_{INA} = 1 + 49.4\,\text{k}\Omega / 619\,\Omega = 80.806$. La salida del amplificador pasa por el divisor $R_{24}/R_{26} = 10\,\text{k}\Omega/10\,\text{k}\Omega$ (con $C_8 = 10\,\mu\text{F}$ como filtro), por lo que la ganancia efectiva vista por el ADC es $G_{efectiva} = 80.806 / 2 = 40.403$. La fórmula de conversión de la Sección 4.2 ya incluye este factor.
   * Incluye rutina de **compensación de resistencia de cable por software ($\Delta R_{cable}$)** almacenada en EEPROM para anular el efecto óhmico del tendido de cables antes de calcular la temperatura mediante la ecuación de Callendar-Van Dusen.
   * **Puesta en servicio del bus:** tanto el `CWT-TM-16PT` como el analizador `JSY-MK-339` salen de fábrica en dirección `1` y `9600 8N1`. Cada módulo se programa con su dirección definitiva (por ejemplo, `CWT = 1`, `JSY = 2`) **antes** de conectarlo al bus; el sistema no admite dos esclavos con la misma dirección.

### 2.3. Entradas Digitales de Estado de Compresores (Retroalimentación 24V – 48V DC)
* **Función:** Detectar el encendido y apagado real de cada compresor leyendo los contactos auxiliares secos (NO/NC) de los contactores de fuerza energizados con tensión de planta ($24\text{ V DC}$ a $48\text{ V DC}$).
* **Acondicionamiento en placa (C-02):** La placa BrightC V3 incorpora la electrónica de acondicionamiento y aislamiento para el rango $24\text{–}48\text{ V DC}$ **antes** del pin del expansor `MCP23S17`. Los contactos secos se conectan directamente a las entradas designadas, sin necesidad de hardware externo adicional.
* **Filtrado Digital (*Debounce*):** Muestreo a $50\text{ Hz}$ (cada $20\text{ ms}$) exigiendo una estabilidad continua de **$500\text{ ms}$ (25 muestras idénticas consecutivas)** antes de validar el cambio de estado de un compresor, eliminando falsos conteos por rebote mecánico o vibración de bobinas.

### 2.4. Medición Eléctrica Totalizada y Humedad de Sala
* **Analizador de Energía Trifásico (`JSY-MK-339`):** Conectado por **Modbus RTU** (RS-485 a $9600\text{ bps}$) midiendo en la acometida general del tablero de climatización: voltajes por fase, corrientes de línea, Potencia Activa Total ($P_{elec\_total}$ en $\text{kW}$, registro `0x0052`), Factor de Potencia y Energía Acumulada ($\text{kWh}$).
* **Transductor Higrométrico ($HR_{sala}$):** Sensor digital $\text{I}^2\text{C}$ integrado para registrar el porcentaje de humedad relativa ($\%HR$) del ambiente de la sala. El modelo específico se definirá en la ingeniería de detalle (C-03); el contrato de firmware es un sensor $\text{I}^2\text{C}$ con lectura de humedad relativa.

### 2.5. Sensórica Modular $4\text{–}20\text{ mA}$ (Reserva de Diseño para Fase 2 / Futuras Implementaciones)
* **Alcance en Fase 1 (Piloto):** No se instalarán ni habilitarán en el firmware inicial los sensores de lazo de corriente $4\text{–}20\text{ mA}$ (**Sensor Tipo 1:** Presión manométrica de refrigerante R410A de $0\text{ a }45\text{ bar}$; **Sensor Tipo 2:** Presión diferencial de aire en filtros de $0\text{ a }500\text{ Pa}$).
* **Previsión de Ingeniería:** El hardware del BrightC V3 conserva libres los 10 canales analógicos del conector `J9 (ADCx10)` y se deja constancia formal en la Sección 4.6 y Sección 5.3 del presente documento del modelo matemático ampliado para su activación en etapas posteriores.

---

## 3. MÁQUINA DE ESTADOS DE ESTABILIZACIÓN Y DESAGREGACIÓN ELÉCTRICA

Dado que los compresores son gobernados por un controlador externo y conmutan de manera asíncrona, el **STM32H7** ejecuta una máquina de estados temporal para cada compresor $c$, evitando evaluar ecuaciones termodinámicas durante los transitorios físicos:

1. **Estado 0 — Apagado (`OFF`):**
   * El contacto auxiliar está abierto y se contabiliza el tiempo continuo de parada ($t_{off}$).
   * **Detección de Ciclo Corto:** Si el compresor vuelve a encenderse cuando $t_{off} < 180\text{ segundos}$, se incrementa el contador de **Alerta de Desgaste por Ciclos Cortos**.
2. **Estado 1 — Amortiguación de Arranque Eléctrico (`INRUSH_WAIT`, $0 < t_{on} < 15\text{ s}$):**
   * Se descartan las lecturas eléctricas durante el pico de corriente de arranque (*inrush*) del motor.
3. **Estado 2 — Desagregación por Escalón de Potencia (`STEP_LOAD_CAPTURE`, $t_{on} = 15\text{ s}$):**
   * Con un único medidor eléctrico global, el sistema identifica la potencia individual consumida por el compresor que acaba de entrar midiendo el salto neto de potencia activa respecto al segundo previo a su arranque (siempre que ningún otro compresor haya conmutado en esa ventana de $15\text{ s}$):
     $$\Delta P_{comp,c} = P_{elec\_total}(t_{on} = 15\text{ s}) - P_{elec\_total}(t_{on} = 0^-)$$
4. **Estado 3 — Estabilización Térmica de Ducto (`THERMAL_WARMUP`, $15\text{ s} < t_{on} < 180\text{ s}$):**
   * Se muestran las temperaturas en vivo en la pantalla HMI y vía Modbus/API, pero **se bloquea el cálculo de alertas predictivas y la regresión histórica**, dando tiempo a que el serpentín evaporador y las sondas PT100 alcancen su equilibrio térmico.
5. **Estado 4 — Régimen Permanente Válido (`STEADY_STATE`, $t_{on} \ge 180\text{ s}$):**
   * Habilita la evaluación del modelo termodinámico, el cálculo del COP y la alimentación del registro histórico horario.

---

## 4. MOTOR MATEMÁTICO TERMODINÁMICO Y PSICROMÉTRICO (FASE 1: PILOTO)

### 4.1. Procesamiento de la Matriz de Temperaturas $(N + N) + M$
Para cada ciclo de muestreo, el sistema procesa los $N$ pares de climatización ($k = 1 \dots N$, con $N=6$ en el piloto) y los $M$ sensores de ambiente de sala ($m = 1 \dots M$, con $M=2$ en el piloto):

1. **Diferencial Térmico por Par ($\Delta T_k$):**
   $$\Delta T_k = T_{ret,k} - T_{inj,k} \quad \forall k \in \{1 \dots N\}$$
2. **Temperaturas Medias y Diferencial Global de Climatización:**
   Si los $N$ pares muestrean un pleno común o operan activos simultáneamente (con $N_{activos}$ pares asociados a flujos encendidos):
   $$\overline{T}_{inj} = \frac{1}{N_{activos}} \sum_{k \in \text{Activos}} T_{inj,k} \quad ; \quad \overline{T}_{ret} = \frac{1}{N_{activos}} \sum_{k \in \text{Activos}} T_{ret,k}$$
   $$\overline{\Delta T}_{clima} = \overline{T}_{ret} - \overline{T}_{inj}$$
3. **Indicadores Térmicos del Ambiente de Sala ($M$ sensores):**
   Se calcula la temperatura media de la sala ($\overline{T}_{sala}$), la temperatura máxima de punto caliente ($T_{sala,max}$) y el **Gradiente de Estratificación / Cortocircuito de Aire ($\Delta T_{bypass}$)**:
   $$\overline{T}_{sala} = \frac{1}{M} \sum_{m=1}^{M} T_{sala,m} \quad ; \quad T_{sala,max} = \max_{m \in \{1..M\}}\left(T_{sala,m}\right)$$
   $$\Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret}$$
   *(Nota de diagnóstico espacial: Si $\overline{T}_{sala}$ es significativamente mayor que $\overline{T}_{ret}$, el aire frío inyectado está retornando directamente a la succión de las máquinas sin barrer el calor de los racks —cortocircuito de flujo de aire—).*

### 4.2. Conversión Analítica para Canales PT100 Analógicos Internos
Cuando se utilicen los canales analógicos internos del BrightC V3, la resistencia pura del sensor ($R_{PT100}$) se despeja del puente de Wheatstone restando el parámetro de calibración de cable ($\Delta R_{cable}$):

$$V_a = 0.05783\text{ V} + \frac{V_{ADC}}{40.403} \implies R_{PT100} = \left( \frac{5100\,\Omega \cdot V_a}{3.3\text{ V} - V_a} \right) - \Delta R_{cable}$$

Aplicando la ecuación inversa de **Callendar-Van Dusen** ($R_0 = 100\,\Omega$, $A = 3.9083 \times 10^{-3}\,^\circ\text{C}^{-1}$, $B = -5.775 \times 10^{-7}\,^\circ\text{C}^{-2}$):

$$T\,[{}^\circ\text{C}] = \frac{-A + \sqrt{A^2 - 4B\left(1 - \frac{R_{PT100}}{100}\right)}}{2B}$$

### 4.3. Corrección Barométrica por Altitud y Flujo Másico ($\dot{m}$)
Para la altitud geográfica del sitio ($Z$ en metros sobre el nivel del mar), se calcula la presión atmosférica local ($P_{atm}$) y la densidad real del aire ($\rho_{aire}$):

$$P_{atm} = 101.325 \times \left(1 - 2.25577 \times 10^{-5} \cdot Z\right)^{5.25588} \quad [\text{kPa}]$$

$$\rho_{aire} = \frac{P_{atm}}{0.28705 \cdot (\overline{T}_{ret} + 273.15)} \quad [\text{kg/m}^3]$$

En la **Fase 1 (sin sensor de presión diferencial de aire)**, el flujo másico total de aire ($\dot{m}_{total}$ en $\text{kg/s}$) se determina a partir del número de unidades manejadoras de aire activas ($U_{ON}$, aquellas que tienen al menos un compresor encendido) y su caudal volumétrico nominal de diseño ($V_{nom,u}$ en $\text{m}^3/\text{h}$):

$$\dot{m}_{total} = \sum_{u=1}^{U_{ON}} \left(\frac{V_{nom,u}}{3600}\right) \times \rho_{aire} \quad [\text{kg/s}]$$

Si los $N=6$ pares de sensores están asignados a razón de 2 pares por cada una de las 3 unidades manejadoras (o 1 par por cada uno de los 6 circuitos de compresión), el flujo másico se pondera equitativamente entre los pares de las unidades activas; si muestrean un pleno común, el flujo másico total se multiplica por el diferencial promedio $\overline{\Delta T}_{clima}$.

### 4.4. Auditoría de Calor Sensible ($Q_{sensible}$)
La potencia frigorífica sensible real dedicada a neutralizar el calor disipado por el equipamiento de telecomunicaciones ($C_{p,aire} = 1.005\text{ kJ/kg}\cdot^\circ\text{C}$) es:

* **Por Par / Zona $k$ ($k = 1 \dots 6$ en el piloto):**
  $$Q_{sensible,k} = \dot{m}_k \cdot C_{p,aire} \cdot \left(T_{ret,k} - T_{inj,k}\right) \quad [\text{kW térmicos}]$$
* **Total Sala:**
  $$Q_{sensible\_total} = \dot{m}_{total} \cdot C_{p,aire} \cdot \overline{\Delta T}_{clima} \quad [\text{kW térmicos}]$$

### 4.5. Estimación Psicrométrica de Calor Latente ($Q_{latente}$) y Eficiencia ($COP_{real}$)
A partir de la temperatura media de retorno ($\overline{T}_{ret}$) y la humedad relativa de la sala ($HR_{sala}$):

1. **Presión de Vapor de Saturación ($P_{ws,ret}$), Presión Parcial ($P_{w,ret}$) y Punto de Rocío ($T_{rocio}$):**
   $$P_{ws,ret} = 0.61078 \cdot \exp\left(\frac{17.27 \cdot \overline{T}_{ret}}{\overline{T}_{ret} + 237.3}\right) \quad [\text{kPa}] \quad ; \quad P_{w,ret} = P_{ws,ret} \cdot \left(\frac{HR_{sala}}{100}\right)$$
   $$\gamma = \ln\left(\frac{P_{w,ret}}{0.61078}\right) \implies T_{rocio} = \frac{237.3 \cdot \gamma}{17.27 - \gamma} \quad [{}^\circ\text{C}]$$
2. **Humedad Absoluta de Entrada ($\omega_{ret}$) y Salida ($\omega_{inj,k}$):**
   $$\omega_{ret} = 0.62198 \cdot \frac{P_{w,ret}}{P_{atm} - P_{w,ret}} \quad [\text{kg}_{agua}/\text{kg}_{aire\_seco}]$$
   Si en un par $k$ la temperatura de inyección cae por debajo del punto de rocío ($T_{inj,k} < T_{rocio}$), se produce condensación sobre el serpentín (asumiendo salida de aire a $HR_{inj} \approx 92\%$, donde $P_{w,inj,k} = 0.92 \cdot P_{ws}(T_{inj,k})$):
   $$\omega_{inj,k} = 0.62198 \cdot \frac{P_{w,inj,k}}{P_{atm} - P_{w,inj,k}}$$
3. **Potencia Térmica Latente ($Q_{latente\_total}$) y Fracción de Desperdicio ($\%Latente$):**
   Con entalpía de vaporización $h_{fg} = 2450\text{ kJ/kg}$:
   $$Q_{latente\_total} = \sum_{k=1}^{N} \dot{m}_k \cdot h_{fg} \cdot \max\left(0, \; \omega_{ret} - \omega_{inj,k}\right) \quad [\text{kW térmicos}]$$
   $$\% \text{Calor Latente} = \left( \frac{Q_{latente\_total}}{Q_{sensible\_total} + Q_{latente\_total}} \right) \times 100$$
4. **Coeficiente de Rendimiento Dinámico ($COP_{real}$):**
   $$COP_{real} = \frac{Q_{sensible\_total} + Q_{latente\_total}}{P_{elec\_total}}$$

---

### 4.6. Modelo Matemático Reservado para Fase 2 (Con Sensores $4\text{–}20\text{ mA}$)
*(Constancia de diseño para implementaciones futuras cuando se habiliten los sensores de presión en la configuración)*:

1. **Corrección Dinámica de Caudal con Sensor Tipo 2 ($\Delta P_{aire}$ de $0\text{ a }500\text{ Pa}$ en Filtros):**
   Cuando se instale el transmisor de presión diferencial en el filtro, el flujo másico $\dot{m}$ dejará de ser constante y se corregirá automáticamente ante la pérdida de carga del filtro ($\alpha \approx 0.35$):
   $$\dot{m}_{corregido} = \dot{m}_{nominal} \times \sqrt{\max\left(0.20, \; 1.0 - \alpha \cdot \frac{\Delta P_{aire} - \Delta P_{limpio}}{\Delta P_{max} - \Delta P_{limpio}}\right)}$$
2. **Termodinámica de Refrigerante con Sensor Tipo 1 ($P_{gas}$ de $0\text{ a }45\text{ bar}$ en R410A):**
   Cuando se instale el transmisor de presión en la línea de succión del compresor, se calculará la temperatura de saturación de evaporación ($T_{sat}$) y el sobrecalentamiento útil ($\Delta T_{SH}$):
   $$T_{sat}\,[{}^\circ\text{C}] = -26.5 + 4.82 \cdot P_{bar} - 0.115 \cdot P_{bar}^2 \quad ; \quad \Delta T_{SH} = T_{inj,k} - T_{sat}$$

---

## 5. ANALÍTICA PREDICTIVA Y MATRIZ DE DIAGNÓSTICOS

### 5.1. Firma Térmica Base (*Baseline*) y Regresión Matricial (OLS)
Durante los primeros **7 días (168 horas)** de operación inicial, el sistema registra el comportamiento óptimo de los $N$ pares térmicos ($N=6$ en el piloto), las $M$ sondas de sala ($M=2$ en el piloto) y la potencia eléctrica, resolviendo localmente mediante **Mínimos Cuadrados Ordinarios (OLS)** los coeficientes cinemáticos de la sala:

$$\widehat{COP}_{esperado} = \beta_0 + \beta_1 \cdot \overline{T}_{sala} + \beta_2 \cdot P_{elec\_total}$$

El vector $\beta = (X^T X)^{-1} X^T Y$ y los diferenciales base de cada par ($\Delta T_{base,k}$, $T_{inj\_base,k}$) se guardan en la memoria **EEPROM**. El desgaste acumulado del sistema se calcula continuamente como:

$$\% \text{Degradación Mecánica} = \text{clamp}_{0..100}\left( \frac{\widehat{COP}_{esperado} - COP_{real}}{\widehat{COP}_{esperado}} \times 100 \right)$$

### 5.2. Reglas de Diagnóstico Predictivo para Fase 1 (Basadas en Matriz $N+N+M$ y Energía)

1. **Predicción de Obstrucción de Filtros (Caudal Estrangulado en Par $k$):**
   * **Física:** Al obstruirse un filtro, el caudal de aire cae drásticamente; el aire permanece más tiempo en contacto con el serpentín frío, provocando que $T_{inj,k}$ descienda anormalmente hacia el umbral de congelamiento ($9.0\,^\circ\text{C}$) mientras $\Delta T_k$ aumenta por encima de su valor base ($\Delta T_{base,k}$).
   * **Índice de Probabilidad ($0\text{ a }100\%$):**
     $$\% \text{Filtro}_k = \text{clamp}_{0..100}\left( 60 \cdot \frac{T_{inj\_base,k} - T_{inj,k}}{3.0^\circ\text{C}} + 40 \cdot \frac{\Delta T_k - \Delta T_{base,k}}{4.0^\circ\text{C}} \right)$$
   * **Estimación de Días Restantes para Limpieza:** Calculando en memoria RAM la pendiente diaria de caída de la temperatura de inyección ($m_{T,k}$ en $^\circ\text{C}/\text{día}$) durante los últimos 7 a 30 días:
     $$\text{Días Restantes}_k = \frac{T_{inj,k} - 7.5^\circ\text{C}}{|m_{T,k}|}$$

2. **Predicción de Fuga de Refrigerante o Fatiga de Compresor (En Par $k$):**
   * **Física:** El consumo eléctrico de los compresores activos se mantiene elevado ($P_{elec\_total}$ normal), pero la transferencia térmica en el par $k$ decae ($\Delta T_k$ disminuye progresivamente respecto a $\Delta T_{base,k}$ ajustado por la fracción de compresores encendidos $F_{carga}$) y $T_{inj,k}$ se vuelve tibia ($> 15\,^\circ\text{C}$).
   * **Índice de Probabilidad ($0\text{ a }100\%$):**
     $$\% \text{Fuga}_k = \text{clamp}_{0..100}\left( \frac{(\Delta T_{base,k} \cdot F_{carga}) - \Delta T_k}{0.5 \cdot (\Delta T_{base,k} \cdot F_{carga})} \times 100 \right) \quad \text{(evaluado si } \% \text{Filtro}_k < 50\% \text{)}$$

3. **Detección de Punto Caliente (*Hotspot*) o Mala Distribución de Aire en Sala (Usando los $M$ sensores):**
   * **Divergencia Espacial en Sala:** Se activa si la diferencia entre los sensores de sala supera el umbral de homogeneidad ($|T_{sala,1} - T_{sala,2}| > 3.5\,^\circ\text{C}$) o si $T_{sala,max} > 27.0\,^\circ\text{C}$ mientras $\overline{T}_{ret} < 23.0\,^\circ\text{C}$ (indicando que el flujo de frío no está llegando a la zona de servidores).

4. **Detección de Pérdida de Hermeticidad / Infiltración de Humedad:**
   * Se dispara cuando $\% \text{Calor Latente} > 30\%$ del esfuerzo térmico total o cuando $HR_{sala} > 65\%$ de forma sostenida.

5. **Predicción de Desgaste Electromecánico por Ciclos Cortos:**
   * Se activa cuando el conteo de re-arranques con $t_{off} < 180\text{ s}$ de cualquier compresor supera 3 eventos en una misma hora.

### 5.3. Ampliación de Diagnóstico Reservada para Fase 2 (Con Sensores $4\text{–}20\text{ mA}$)
*(Constancia funcional para siguientes etapas)*:
* Al habilitar el **Sensor Tipo 2 ($\Delta P_{aire}$)**, el porcentaje de obstrucción de filtro pasará a medirse directamente por diferencia de presiones: $\% \text{Filtro} = \left(\frac{\Delta P_{aire} - \Delta P_{limpio}}{\Delta P_{max} - \Delta P_{limpio}}\right) \times 100$.
* Al habilitar el **Sensor Tipo 1 ($P_{gas}$)**, la alerta de fuga de refrigerante se confirmará directamente cuando $P_{bar} < 6.5\text{ bar}$ ($T_{sat} < 0\,^\circ\text{C}$) con filtro limpio, y se aislará la falla mecánica de válvulas del compresor cuando $P_{bar} > 11.5\text{ bar}$ con el contactor cerrado.

### 5.4. Auditoría de Capacidad y Reserva Frigorífica Libre ($kW_{libres}$)
Para una sala donde se exige mantener una redundancia operativa (máximo $U_{req}$ unidades necesarias para cubrir la carga sin comprometer la unidad de respaldo en un arreglo $2+1$):

$$Q_{max\_seguro} = \left(\sum_{u=1}^{U_{req}} Q_{nominal,u}\right) \times \left(1.0 - \frac{\% \text{Degradación Mecánica}}{100}\right)$$

$$kW_{libres} = \max\left(0.0, \; Q_{max\_seguro} - Q_{sensible\_total}\right)$$

---

## 6. ARQUITECTURA DE DATOS Y ALMACENAMIENTO HÍBRIDO (RAM + EEPROM + MICROSD)

El sistema evita el desgaste prematuro de la tarjeta MicroSD y maximiza la velocidad de cálculo repartiendo los datos en tres memorias según su naturaleza temporal:

### 6.1. Nivel 1: Memoria RAM Interna del STM32H7 (Cálculo en Vivo y Ventana de 30 Días)
* **Acumuladores de Tiempo Real (Segundo a Segundo):** Todas las lecturas de los $2N + M$ sensores de temperatura (**14 sensores en el piloto: $6+6+2$**), humedad y energía se filtran y promedian cada segundo exclusivamente en la memoria RAM.
* **Buffer Circular Histórico (*Ring Buffer* de 720 Horas = 30 Días):** La RAM conserva de forma permanente los últimos **720 resúmenes horarios**. Con un tamaño estructurado de **64 bytes por registro horario** (que soporta hasta $N=8$ pares y $M=4$ sensores de sala, cubriendo holgadamente el piloto $6+6+2$), los 30 días completos ocupan únicamente **$46.08\text{ KB}$** del megabyte de RAM disponible en el STM32H7.
* **Cero Latencia:** Las regresiones matriciales (OLS), el cálculo de pendientes diarias y las consultas de la API REST de hasta 30 días se ejecutan leyendo directamente la RAM en microsegundos.

### 6.2. Nivel 2: Memoria EEPROM $\text{I}^2\text{C}$ (`AT24C256C` de 32 KB — Configuración y Firma Base)
Resguarda de forma inmutable:
1. **Parámetros de Topología del Sitio:** Número de pares configurados ($N$, con $N=6$ en el piloto), número de sensores de sala ($M$, con $M=2$ en el piloto), mapeo de direcciones Modbus del módulo `CWT-TM-16PT`, cantidad de equipos HVAC, compresores y altitud del local.
2. **Calibración y Firma Térmica del Día 1 (*Baseline*):** Coeficientes $\beta_0, \beta_1, \beta_2$ del modelo OLS, diferenciales iniciales por par ($\Delta T_{base,1..N}$, $T_{inj\_base,1..N}$) y el consumo eléctrico aprendido por cada compresor ($\Delta P_{comp,c}$).

### 6.3. Nivel 3: Tarjeta MicroSD Industrial (Datalogger Binario Indexado de Largo Plazo)
* **Estructura del Bloque Horario en MicroSD (64 bytes fijos por hora):**
  * `Timestamp UNIX` (4 bytes)
  * `Vector de Temperaturas de Inyección Promedio` ($N$ valores compactados en décimas de $^\circ\text{C}$, hasta 8 pares —cubre los 6 del piloto—: 16 bytes)
  * `Vector de Temperaturas de Retorno Promedio` ($N$ valores compactados en décimas de $^\circ\text{C}$, hasta 8 pares —cubre los 6 del piloto—: 16 bytes)
  * `Vector de Temperaturas de Sala Promedio` ($M$ valores compactados en décimas de $^\circ\text{C}$, hasta 4 sensores —cubre los 2 del piloto—: 8 bytes)
  * `Humedad Relativa Promedio` (2 bytes - décimas de $\%HR$)
  * `Potencia Eléctrica Activa Promedio` (4 bytes - Float $\text{kW}$)
  * `Capacidad Térmica Sensible y Latente Total` (8 bytes - 2x Float $\text{kW}$)
  * `COP Efectivo Promedio` (2 bytes - centésimas)
  * `Máscara de Estado de Compresores y Conteo de Arranques` (2 bytes)
  * `Reserva Fase 2 (Presiones) + Checksum CRC-16` (2 bytes)
* **Escritura Única Horaria:** Solo una vez cada 60 minutos se realiza un `Append` de 64 bytes en la MicroSD. Un año completo ($8,760\text{ horas}$) ocupa apenas **$560.6\text{ KB}$**.
* **Reconstrucción Instantánea al Arranque (*Boot Restore*):** Ante un corte eléctrico total en la estación, al volver la energía el STM32H7 lee los últimos $46.08\text{ KB}$ de la MicroSD y restaura en menos de $10\text{ ms}$ los 30 días de historia en la memoria RAM.

### 6.4. Requisito de Plataforma: Repartición de Flash para la Imagen con Red (C-05)
La imagen de aplicación que integra la pila de red (Ethernet/WiFi, sockets, SNMP, HTTP y TLS) supera el tamaño del slot actual de $256\text{ KB}$. La arquitectura OTA del proyecto (`doc/architecture/mcuboot_external_flash_ota_architecture.md`) ya define la repartición asimétrica prevista: slot 0 en flash interna, slot 1 en la QSPI externa ($1.75\text{ MB}$) y una partición LittleFS de $256\text{ KB}$.

Esta repartición es un **prerrequisito de la fase de interfaces**, no un cambio de la presente especificación. El DTS no se modifica en esta revisión.

---

## 7. INTERFACES DE COMUNICACIÓN Y VISUALIZACIÓN

1. **Pantalla Táctil Local (HMI DWIN 4.3"):**
   * Presenta en la vista principal los promedios globales ($\overline{T}_{ret}$, $\overline{T}_{inj}$, $\overline{\Delta T}_{clima}$, $\overline{T}_{sala}$), Humedad Relativa, Potencia Eléctrica ($\text{kW}$), Capacidad Frigorífica ($\text{kW}$), $COP_{real}$ y alertas predictivas activas, contando con una sub-pantalla de detalle para inspeccionar individualmente los **$6$ pares de Inyección/Retorno ($T_{inj,1..6}$, $T_{ret,1..6}$, $\Delta T_{1..6}$)** y los **$2$ sensores de sala ($T_{sala,1..2}$)**.
2. **Modbus TCP Server (Integración con Huawei NetEco / SCADA):**
   * El controlador opera como **Modbus TCP Server** (Puerto 502) sobre la interfaz Ethernet integrada, soportando múltiples conexiones simultáneas de clientes/maestros. Mapea en bloques contiguos de *Holding Registers* (`0x03`) e *Input Registers* (`0x04`) los valores globales de la sala, el detalle individual de cada uno de los $N=6$ pares térmicos, las $M=2$ temperaturas de sala, los parámetros del analizador de energía, el margen de reserva libre ($kW_{libres}$), el porcentaje de degradación mecánica y la matriz de códigos de alerta predictiva.
3. **Agente SNMP (Simple Network Management Protocol v2c / v3):**
   * Expone una **MIB (Management Information Base)** estructurada con los OIDs correspondientes a las variables termodinámicas, eléctricas ($P_{elec\_total}$, $COP_{real}$, $kW_{libres}$), lecturas de los pares de temperatura ($6+6+2$) e indicadores de degradación para consulta directa (*SNMP GET / WALK* en puerto UDP 161) desde gestores de red corporativos (NMS como Zabbix, PRTG, SolarWinds o sistemas OSS de Entel).
   * Incluye emisión asíncrona de **SNMP Traps / Informs** (puerto UDP 162) para notificar de forma inmediata la activación o despeje de alertas predictivas (obstrucción de filtros, posible fuga de refrigerante, puntos calientes en sala, pérdida de aislamiento o ciclos cortos).
4. **API RESTful Local (HTTP/JSON):**
   * Expone los endpoints `GET /api/v1/hvac/status` (con el desglose del arreglo de pares `pares_termicos[1..6]`, `sensores_sala[1..2]`, telemetría global y analítica), `GET /api/v1/hvac/logs?hours=X` y `POST /api/v1/hvac/config`, protegidos mediante autenticación *Bearer Token* y lista blanca de direcciones IP (*IP Whitelisting*).