# ESPECIFICACIÓN TÉCNICA Y MODELO MATEMÁTICO DE INGENIERÍA (REVISIÓN F — CIERRE DE PROYECTO)
**Proyecto:** Sistema de Telemetría, Auditoría Energética y Analítica Predictiva en el Borde (*Edge Computing*) para Salas de Climatización Crítica (Data Centers / Nodos Telco)  
**Plataforma de Hardware:** Controlador Industrial BrightC V3 (Arquitectura ARM Cortex-M7 STM32H7 con unidad DP-FPU de 64 bits)  
**Estado del Documento:** Revisión F (Versión Definitiva de Cierre de Ingeniería) — Corrección del flujo másico vinculado a contactores de ventiladores de inyección $U_{vent\_ON}$ (F-01), adopción de Tabla de Búsqueda (LUT de 46 puntos) para R410A en Fase 2 (F-02), exigencia de aritmética de doble precisión IEEE 754 (`double` de 64 bits en DP-FPU) con regularización de Tikhonov para la inversión matricial OLS (F-03) y regla de precedencia/inicialización de 168 horas en la proyección de días restantes de filtros (F-04). Sustituye a la Revisión E.

### Registro de cambios (E → F)

| ID | Sección | Cambio |
| --- | --- | --- |
| F-01 | 2.3, 4.3.4 | **Desacoplamiento entre Ventilación y Compresión ($U_{vent\_ON}$):** Se corrige la sumatoria del flujo másico de aire seco ($\dot{m}_{da,total}$) sustituyendo $U_{ON}$ (compresores) por $U_{vent\_ON}$, mapeado estrictamente al estado activo de los contactores de los **ventiladores de inyección del evaporador**. Esto preserva la validez del balance entálpico cuando los compresores apagan por termostato pero las manejadoras continúan recirculando aire en la sala. |
| F-02 | 4.6.2 | **Curva de Saturación de R410A mediante LUT de 46 Puntos en Flash:** Se elimina la ecuación logarítmica inversa de Antoine (evitando errores de desplazamiento de escala y el costo computacional de `ln()` a $50\text{ Hz}$) y se establece como estándar para la Fase 2 una **Tabla de Búsqueda (LUT estática de 46 puntos, de $0\text{ a }45\text{ bar}$)** almacenada en memoria Flash con interpolación lineal determinista en $\mathcal{O}(1)$. |
| F-03 | 5.1 | **Álgebra Lineal OLS en Doble Precisión (`double` 64 bits) en DP-FPU:** Para mitigar el mal condicionamiento numérico (colinealidad de $\overline{T}_{ret}$ en salas estrechamente reguladas), se exige que el acumulador de momentos $X^T X$, el vector $X^T Y$ y la inversión matricial se ejecuten obligatoriamente en punto flotante de **doble precisión (`double` de 64 bits)** utilizando la unidad **DP-FPU por hardware del STM32H7**, incorporando además regularización diagonal ($\lambda I$) contra matrices singulares. |
| F-04 | 5.2.1 | **Máquina de Precedencia y Bloqueo de Inicialización en "Días Restantes":** Se establece una jerarquía determinista para los registros Modbus TCP / SNMP / API REST: durante las primeras $168\text{ horas}$ (buffer histórico incompleto), el sistema reporta el código fijo de inicialización (`100 días` en telemetría / `0xFFFF` en estado crudo); una vez cumplido el periodo de aprendizaje, si $\% \text{Filtro}_k < 25\%$ o $m_{T,k} \ge 0$, se satura en el tope de `90 días`, aplicando la ecuación diferencial acotada únicamente cuando existe tendencia real de ensuciamiento. |

---

## 1. ALCANCE Y FILOSOFÍA DE OPERACIÓN

El sistema **BrightC V3** opera como una unidad de auditoría energética y procesamiento analítico local (*Edge Processing*) que monitorea en tiempo real el desempeño termodinámico, psicrométrico, eléctrico y mecánico de sistemas de aire acondicionado en salas críticas de telecomunicaciones (arreglos redundantes $2+1$, $3+1$, etc.).

El controlador actúa como un **observador pasivo en paralelo** (sin interferir con el lazo de control de los termostatos existentes en el sitio), adquiriendo variables físicas directas (incluyendo condiciones de frontera interiores y exteriores) para calcular balances de masa, energía y entropía/exergía (ciclo de Carnot/Rankine inverso), aislar transitorios electromecánicos, aprender la firma térmica de la sala y emitir diagnósticos predictivos hacia plataformas de gestión superior mediante **Modbus TCP Server** (Huawei NetEco), **SNMP** y una **API RESTful Local (HTTP/JSON)**, además de su visualización en pantalla táctil local (**HMI DWIN 4.3"**).

---

## 2. ARQUITECTURA CONFIGURABLE DE HARDWARE Y SENSÓRICA

El diseño del sistema es **100% paramétrico y dirigido por configuración**. Permite definir dinámicamente la cantidad de pares de sensores de temperatura interior, sensores de ambiente de sala, sensores de temperatura exterior, unidades manejadoras de aire (HVAC), ventiladores de inyección y compresores por unidad.

### 2.1. Topología Matricial de Temperatura: Esquema $(N + N) + M + L$
La adquisición de temperatura se estructura bajo una arquitectura de **pares térmicos apareados ($N + N$)**, una red de **monitoreo ambiental de sala ($M$)** y sensores de **referencia climática exterior ($L$)**:

1. **Pares de Evaluación Termodinámica en Evaporador ($N$ Inyección + $N$ Retorno):**
   * Los puntos de medición sobre el flujo de climatización interior se instalan **siempre en pares ($1+1$, $2+2$, $\dots$, $N+N$)**.
   * Cada par $k \in \{1 \dots N\}$ está compuesto por una sonda de **Inyección de Aire Frío ($T_{inj,k}$)** y su correspondiente sonda de **Retorno de Aire Caliente ($T_{ret,k}$)**.
2. **Sensores de Temperatura Ambiente de Sala ($M$ Sensores):**
   * De forma independiente a los pares de ducto/equipo, el sistema incorpora $M$ sensores adicionales ($T_{sala,m}$, con $m \in \{1 \dots M\}$) distribuidos en los pasillos o zonas críticas de racks para auditar la temperatura real del recinto, detectar puntos calientes (*hotspots*) y evaluar la efectividad del barrido térmico en la sala.
3. **Sensores de Temperatura Exterior / Ambiente de Condensación ($L$ Sensores):**
   * Se incorporan $L$ sensores de temperatura en el exterior del recinto ($T_{ext,l}$, con $l \in \{1 \dots L\}$), ubicados en la vecindad de la entrada de aire de las unidades condensadoras exteriores (protegidos de la radiación solar directa).
   * Permiten medir la temperatura real del foco caliente hacia el cual los compresores rechazan el calor, variable indispensable para evaluar el límite termodinámico de Carnot, el *derating* de capacidad máxima y el ciclo de compresión de vapor (Rankine inverso).
4. **Dimensionamiento para el Primer Sitio Piloto ($6 + 6 + 2 + 2 = 16\text{ Sondas PT100}$):**
   * **$N = 6$ Sondas de Aire Frío (Inyección):** $T_{inj,1} \dots T_{inj,6}$.
   * **$N = 6$ Sondas de Aire Caliente (Retorno):** $T_{ret,1} \dots T_{ret,6}$.
   * **$M = 2$ Sondas de Temperatura de Sala (Ambiente):** $T_{sala,1}, T_{sala,2}$.
   * **$L = 2$ Sondas de Temperatura Exterior (Condensación):** $T_{ext,1}, T_{ext,2}$.
   * **Total Piloto:** **16 sensores RTD PT100 Clase A** de 3 hilos con malla apantallada.

### 2.2. Canales de Adquisición de Temperatura (Interno ADC vs. Externo Modbus RS-485)
Para cubrir desde configuraciones pequeñas hasta despliegues densos como el piloto de 16 sensores ($6+6+2+2$) o superiores, el sistema soporta dos vías de adquisición combinables por configuración:

1. **Módulo de Adquisición Digital RS-485 (`CWT-TM-16PT` — Vía Principal para el Piloto $6+6+2+2$):**
   * Concentrador industrial de **16 canales PT100** con compensación nativa de 3 hilos en bornera, conectado al bus RS-485 del BrightC V3 mediante protocolo **Modbus RTU**.
   * Permite conectar las **16 sondas PT100 del piloto** en un único módulo (`CH1` a `CH6` para Inyección, `CH7` a `CH12` para Retorno, `CH13` a `CH14` para Ambiente de Sala y `CH15` a `CH16` para Temperatura Exterior), entregando todas las lecturas en una sola transacción Modbus con resolución de $0.1\,^\circ\text{C}$ o punto flotante IEEE 754.
   * Los 5 canales analógicos RTD internos de la placa BrightC V3 quedan 100% libres como reserva o expansión adicional.
2. **Etapa Analógica Interna en Placa BrightC V3 (Hasta 5 canales RTD con `INA826`):**
   * Disponible para sitios compactos o como canales suplementarios. Basada en puente de Wheatstone a $3.3\text{ V DC}$ ($R_{sup} = 5.1\text{ k}\Omega$ al $0.1\%$, referencia fija de $91\,\Omega$), amplificador de instrumentación **INA826** ($G_{efectiva} = 40.403$) y lectura por el ADC de 16 bits del STM32H7.
   * **Cadena de ganancia verificada:** el INA826 opera con $R_g = 619\,\Omega$ entre sus pines 1 y 8, lo que da $G_{INA} = 1 + 49.4\,\text{k}\Omega / 619\,\Omega = 80.806$. La salida del amplificador pasa por el divisor $R_{24}/R_{26} = 10\,\text{k}\Omega/10\,\text{k}\Omega$ (con $C_8 = 10\,\mu\text{F}$ como filtro), por lo que la ganancia efectiva vista por el ADC es $G_{efectiva} = 80.806 / 2 = 40.403$.
   * Incluye rutina de **compensación de resistencia de cable por software ($\Delta R_{cable}$)** almacenada en EEPROM para anular el efecto óhmico del tendido de cables antes de calcular la temperatura mediante la ecuación de Callendar-Van Dusen.
   * **Puesta en servicio del bus:** tanto el `CWT-TM-16PT` como el analizador `JSY-MK-339` salen de fábrica en dirección `1` y `9600 8N1`. Cada módulo se programa con su dirección definitiva (por ejemplo, `CWT = 1`, `JSY = 2`) **antes** de conectarlo al bus; el sistema no admite dos esclavos con la misma dirección.

### 2.3. Entradas Digitales de Estado de Ventiladores de Inyección y Compresores (24V – 48V DC — Cambio F-01)
* **Función Dual (Movimiento de Aire vs. Ciclo de Compresión):** El sistema monitorea a través de las entradas digitales optoacopladas ($24\text{ V DC}$ a $48\text{ V DC}$ hacia el expansor `MCP23S17`) tanto los contactos auxiliares secos (NO/NC) de los **contactores de los ventiladores de inyección del evaporador ($U_{vent\_ON}$)** como los contactos auxiliares de los **contactores de fuerza de cada compresor ($c$)**.
  * En configuraciones donde el ventilador de inyección disponga de un contacto auxiliar dedicado en el tablero, su estado digital determina directamente si la unidad manejadora $u$ está impulsando caudal de aire ($u \in U_{vent\_ON}$).
  * En tableros donde el ventilador opere en marcha continua o enclavado al encendido general de la manejadora, el parámetro de configuración permite vincular $U_{vent\_ON}$ a la señal de marcha del ventilador, independizándolo del ciclado por termostato de los compresores.
* **Acondicionamiento en placa:** La placa BrightC V3 incorpora la electrónica de acondicionamiento y aislamiento para el rango $24\text{–}48\text{ V DC}$ **antes** del pin del expansor `MCP23S17`. Los contactos secos se conectan directamente a las entradas designadas, sin necesidad de hardware externo adicional.
* **Filtrado Digital (*Debounce*):** Muestreo a $50\text{ Hz}$ (cada $20\text{ ms}$) exigiendo una estabilidad continua de **$500\text{ ms}$ (25 muestras idénticas consecutivas)** antes de validar el cambio de estado de un ventilador o compresor, eliminando falsos conteos por rebote mecánico o vibración de bobinas.

### 2.4. Medición Eléctrica Totalizada y Humedad en Rejilla de Retorno
* **Analizador de Energía Trifásico (`JSY-MK-339`):** Conectado por **Modbus RTU** (RS-485 a $9600\text{ bps}$) midiendo en la acometida general del tablero de climatización: voltajes por fase, corrientes de línea, Potencia Activa Total ($P_{elec\_total}$ en $\text{kW}$, registro `0x0052`), Factor de Potencia y Energía Acumulada ($\text{kWh}$).
* **Transmisor Higrométrico Industrial $4\text{–}20\text{ mA}$ en Rejilla de Retorno ($HR_{ret}$):** Transductor de humedad relativa ($0\text{ a }100\,\%HR$) con salida de lazo de corriente **$4\text{–}20\text{ mA}$**, montado directamente en la **rejilla de retorno / succión de aire caliente** y cableado a un canal analógico del conector **`J9 (ADCx10)`** de la placa BrightC V3 (con resistencia *shunt* de precisión a masa y alimentación de $+12\text{ V DC}$ provista en el mismo conector `J9`). La transmisión en corriente de $4\text{–}20\text{ mA}$ garantiza inmunidad total al ruido electromagnético en tiradas largas de cable desde el tablero hasta la rejilla de retorno y permite detectar rotura de cable si $I_{lazo} < 3.5\text{ mA}$.

### 2.5. Sensórica Modular $4\text{–}20\text{ mA}$ de Presión (Reserva de Diseño para Fase 2 / Futuras Implementaciones)
* **Alcance en Fase 1 (Piloto):** No se instalarán ni habilitarán en el firmware inicial los sensores de presión de $4\text{–}20\text{ mA}$ (**Sensor Tipo 1:** Presión manométrica de refrigerante R410A de $0\text{ a }45\text{ bar}$; **Sensor Tipo 2:** Presión diferencial de aire en filtros de $0\text{ a }500\text{ Pa}$).
* **Previsión de Ingeniería:** El conector `J9 (ADCx10)` del BrightC V3 conserva **9 canales analógicos libres** (tras ocupar 1 canal para el transmisor de humedad $HR_{ret}$) y se deja constancia formal en la Sección 4.6 y Sección 5.3 del presente documento del modelo matemático ampliado para su activación en etapas posteriores.

---

## 3. MÁQUINA DE ESTADOS DE ESTABILIZACIÓN Y DESAGREGACIÓN ELÉCTRICA

Dado que los compresores son gobernados por un controlador externo y conmutan de manera asíncrona, el **STM32H7** ejecuta una máquina de estados temporal para cada compresor $c$, evitando evaluar ecuaciones de rendimiento del ciclo de vapor durante los transitorios físicos:

1. **Estado 0 — Compresor Apagado (`OFF`):**
   * El contacto auxiliar del compresor está abierto y se contabiliza el tiempo continuo de parada ($t_{off}$). Si el ventilador de inyección de esa manejadora sigue activo ($u \in U_{vent\_ON}$), el caudal de aire continúa computándose en el balance de mezcla de la sala, pero sin exigir salto térmico de refrigeración a dicho circuito.
   * **Detección de Ciclo Corto:** Si el compresor vuelve a encenderse cuando $t_{off} < 180\text{ segundos}$, se incrementa el contador de **Alerta de Desgaste por Ciclos Cortos**.
2. **Estado 1 — Amortiguación de Arranque Eléctrico (`INRUSH_WAIT`, $0 < t_{on} < 15\text{ s}$):**
   * Se descartan las lecturas eléctricas durante el pico de corriente de arranque (*inrush*) del motor.
3. **Estado 2 — Desagregación por Escalón de Potencia (`STEP_LOAD_CAPTURE`, $t_{on} = 15\text{ s}$):**
   * Con un único medidor eléctrico global, el sistema identifica la potencia individual consumida por el compresor que acaba de entrar midiendo el salto neto de potencia activa respecto al segundo previo a su arranque (siempre que ningún otro compresor o ventilador haya conmutado en esa ventana de $15\text{ s}$):
     $$\Delta P_{comp,c} = P_{elec\_total}(t_{on} = 15\text{ s}) - P_{elec\_total}(t_{on} = 0^-)$$
4. **Estado 3 — Estabilización Térmica de Ducto (`THERMAL_WARMUP`, $15\text{ s} < t_{on} < 180\text{ s}$):**
   * Se muestran las temperaturas en vivo en la pantalla HMI y vía Modbus/API, pero **se bloquea el cálculo de alertas predictivas de refrigeración y la acumulación en la matriz de regresión OLS**, dando tiempo a que el serpentín evaporador y las sondas PT100 alcancen su equilibrio térmico.
5. **Estado 4 — Régimen Permanente Válido (`STEADY_STATE`, $t_{on} \ge 180\text{ s}$):**
   * Habilita la evaluación del modelo termodinámico completo, el cálculo del COP real y de Carnot, y la alimentación del registro histórico horario.

---

## 4. MOTOR MATEMÁTICO TERMODINÁMICO Y PSICROMÉTRICO (FASE 1: PILOTO)

### 4.1. Procesamiento de la Matriz de Temperaturas $(N + N) + M + L$
Para cada ciclo de muestreo, el sistema procesa los $N$ pares de climatización ($k = 1 \dots N$, con $N=6$ en el piloto), los $M$ sensores de ambiente de sala ($m = 1 \dots M$, con $M=2$) y los $L$ sensores de temperatura exterior ($l = 1 \dots L$, con $L=2$):

1. **Diferencial Térmico por Par ($\Delta T_k$):**
   $$\Delta T_k = T_{ret,k} - T_{inj,k} \quad \forall k \in \{1 \dots N\}$$
2. **Temperaturas Medias y Diferencial Global de Climatización:**
   Con $N_{vent\_activos}$ pares asociados a unidades manejadoras cuyo ventilador de inyección se encuentra encendido ($u \in U_{vent\_ON}$):
   $$\overline{T}_{inj} = \frac{1}{N_{vent\_activos}} \sum_{k \in U_{vent\_ON}} T_{inj,k} \quad ; \quad \overline{T}_{ret} = \frac{1}{N_{vent\_activos}} \sum_{k \in U_{vent\_ON}} T_{ret,k}$$
   $$\overline{\Delta T}_{clima} = \overline{T}_{ret} - \overline{T}_{inj}$$
3. **Indicadores Térmicos Paramétricos del Ambiente de Sala ($M$ sensores):**
   Se calcula la temperatura media de la sala ($\overline{T}_{sala}$), los extremos térmicos espaciales ($T_{sala,max}$, $T_{sala,min}$), la **Dispersión Térmica Espacial de Sala ($\Delta T_{sala,max}$)** para cualquier número $M \ge 2$ de sensores configurados, y el **Gradiente de Estratificación / Cortocircuito de Aire ($\Delta T_{bypass}$)**:
   $$\overline{T}_{sala} = \frac{1}{M} \sum_{m=1}^{M} T_{sala,m}$$
   $$T_{sala,max} = \max_{m \in \{1..M\}}\left(T_{sala,m}\right) \quad ; \quad T_{sala,min} = \min_{m \in \{1..M\}}\left(T_{sala,m}\right)$$
   $$\Delta T_{sala,max} = T_{sala,max} - T_{sala,min} \quad ; \quad \Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret}$$
4. **Temperatura Exterior de Referencia para Condensación ($L$ sensores):**
   Para evitar sesgos por radiación solar incidente o recirculación local de aire caliente en una condensadora, la temperatura exterior efectiva ($\overline{T}_{ext}$) se obtiene promediando los $L$ sensores exteriores válidos, o tomando el mínimo si la divergencia entre el máximo y el mínimo supera $4.0\,^\circ\text{C}$:
   $$\overline{T}_{ext} = \begin{cases} \frac{1}{L} \sum_{l=1}^{L} T_{ext,l} & \text{si } \left(\max_{l}(T_{ext,l}) - \min_{l}(T_{ext,l})\right) \le 4.0\,^\circ\text{C} \\ \min_{l \in \{1..L\}}(T_{ext,l}) & \text{si } \left(\max_{l}(T_{ext,l}) - \min_{l}(T_{ext,l})\right) > 4.0\,^\circ\text{C} \end{cases}$$

### 4.2. Conversión Analítica para Canales Analógicos Internos (PT100 y Lazo $4\text{–}20\text{ mA}$)
1. **Canales PT100 Internos (`INA826`):** Cuando se utilicen los canales analógicos RTD internos del BrightC V3, la resistencia pura del sensor ($R_{PT100}$) se despeja del puente de Wheatstone restando el parámetro de calibración de cable ($\Delta R_{cable}$):
   $$V_a = 0.05783\text{ V} + \frac{V_{ADC}}{40.403} \implies R_{PT100} = \left( \frac{5100\,\Omega \cdot V_a}{3.3\text{ V} - V_a} \right) - \Delta R_{cable}$$
   Aplicando la ecuación inversa de **Callendar-Van Dusen** ($R_0 = 100\,\Omega$, $A = 3.9083 \times 10^{-3}\,^\circ\text{C}^{-1}$, $B = -5.775 \times 10^{-7}\,^\circ\text{C}^{-2}$):
   $$T\,[{}^\circ\text{C}] = \frac{-A + \sqrt{A^2 - 4B\left(1 - \frac{R_{PT100}}{100}\right)}}{2B}$$
2. **Canal $4\text{–}20\text{ mA}$ de Humedad en Retorno ($HR_{ret}$ en `J9`):** Con resistencia *shunt* $R_{shunt}$ (p. ej., $150\,\Omega$ al $0.1\%$), la corriente de lazo es $I_{mA} = \frac{V_{ADC}}{R_{shunt}} \times 1000$. Para $I_{mA} \in [4.0, 20.0]\text{ mA}$:
   $$HR_{ret}\,[\%] = \text{clamp}_{0..100}\left( \frac{I_{mA} - 4.0}{16.0} \times 100.0 \right)$$

### 4.3. Propiedades del Aire Húmedo en el Retorno y Flujo Másico de Aire Seco ($\dot{m}_{da}$ — Cambio F-01)
Al estar el transmisor de humedad $4\text{–}20\text{ mA}$ ubicado directamente en la rejilla de retorno ($HR_{ret}$), el estado psicrométrico de entrada al evaporador queda determinado por el par $(\overline{T}_{ret}, HR_{ret})$ y la altitud geográfica del sitio ($Z$ en $\text{m s.n.m.}$):

1. **Presión Barométrica Local ($P_{atm}$):**
   $$P_{atm} = 101.325 \times \left(1 - 2.25577 \times 10^{-5} \cdot Z\right)^{5.25588} \quad [\text{kPa}]$$
2. **Presión de Vapor de Saturación ($P_{ws,ret}$) y Presión Parcial de Vapor en Retorno ($P_{w,ret}$):**
   Mediante la ecuación psicrométrica de **Magnus-Tetens / ASHRAE**:
   $$P_{ws}(\overline{T}_{ret}) = 0.61078 \cdot \exp\left(\frac{17.27 \cdot \overline{T}_{ret}}{\overline{T}_{ret} + 237.3}\right) \quad [\text{kPa}]$$
   $$P_{w,ret} = P_{ws}(\overline{T}_{ret}) \cdot \left(\frac{HR_{ret}}{100}\right) \quad [\text{kPa}]$$
3. **Razón de Humedad (Humedad Absoluta Común) en la Rejilla de Retorno ($\omega_{ret}$):**
   $$\omega_{ret} = 0.62198 \cdot \frac{P_{w,ret}}{P_{atm} - P_{w,ret}} \quad \left[\text{kg}_{\text{vapor}} / \text{kg}_{\text{aire seco}}\right]$$
4. **Densidad de Aire Seco en Retorno ($\rho_{da,ret}$) y Flujo Másico de Aire Seco Gobernado por Ventiladores ($\dot{m}_{da}$ — Corrección F-01):**
   Aplicando la ley de Dalton para mezclas de gases ideales ($R_{da} = 0.287055\text{ kJ/(kg}\cdot\text{K)}$), la masa de aire seco por unidad de volumen que ingresa por la rejilla de retorno es:
   $$\rho_{da,ret} = \frac{P_{atm} - P_{w,ret}}{0.287055 \cdot (\overline{T}_{ret} + 273.15)} \quad \left[\text{kg}_{\text{aire seco}}/\text{m}^3\right]$$
   El flujo másico total de aire seco ($\dot{m}_{da,total}$) depende **exclusivamente de las unidades manejadoras cuyo ventilador de inyección del evaporador se encuentra encendido ($u \in U_{vent\_ON}$)**, independientemente de si sus compresores están activos o apagados por termostato:
   $$\dot{m}_{da,total} = \sum_{u \in U_{vent\_ON}} \left(\frac{V_{nom,u}}{3600}\right) \times \rho_{da,ret} \quad \left[\text{kg}_{\text{aire seco}}/\text{s}\right]$$
   *(De esta forma, cuando una manejadora opera en modo de ventilación/recirculación pura con sus compresores apagados, su caudal másico $\dot{m}_{da,u}$ se mantiene activo en la ecuación de continuidad y balance entálpico de la sala).*

### 4.4. Balance Psicrométrico Exacto en el Evaporador: Calor Sensible, Latente y Entalpía
En un proceso de enfriamiento y deshumidificación en serpentín, el calor específico efectivo del aire húmedo en el retorno es $C_{p,m} = C_{p,da} + \omega_{ret} \cdot C_{p,v} = 1.006 + 1.86 \cdot \omega_{ret} \; [\text{kJ/(kg}_{da}\cdot{}^\circ\text{C)}]$.

1. **Temperatura de Punto de Rocío del Aire de Retorno ($T_{rocio,ret}$):**
   $$\gamma = \ln\left(\frac{P_{w,ret}}{0.61078}\right) \implies T_{rocio,ret} = \frac{237.3 \cdot \gamma}{17.27 - \gamma} \quad [{}^\circ\text{C}]$$
2. **Potencia Frigorífica Sensible Neta ($Q_{sensible}$):**
   Corresponde exclusivamente al enfriamiento sin cambio de fase que neutraliza el calor disipado por los racks de telecomunicaciones en los pares con ventilador activo ($k \in U_{vent\_ON}$):
   * **Por Par $k$ ($k = 1 \dots 6$):**
     $$Q_{sensible,k} = \dot{m}_{da,k} \cdot \left(1.006 + 1.86 \cdot \omega_{ret}\right) \cdot \left(T_{ret,k} - T_{inj,k}\right) \quad [\text{kW térmicos}]$$
   * **Total Sala:**
     $$Q_{sensible\_total} = \sum_{k \in U_{vent\_ON}} Q_{sensible,k} \quad [\text{kW térmicos}]$$
3. **Condición de Condensación y Humedad Absoluta de Inyección ($\omega_{inj,k}$):**
   El proceso psicrométrico en cada par $k$ presenta dos regímenes físicos según la temperatura del aire de descarga ($T_{inj,k}$) frente al punto de rocío de retorno ($T_{rocio,ret}$):
   * **Régimen Seco ($T_{inj,k} \ge T_{rocio,ret}$):** El aire se enfría a humedad absoluta constante ($\omega_{inj,k} = \omega_{ret}$). No existe condensación de agua en el serpentín ($Q_{latente,k} = 0$).
   * **Régimen de Condensación Activa ($T_{inj,k} < T_{rocio,ret}$):** El aire alcanza la línea de saturación del diagrama psicrométrico sobre las aletas del serpentín (con factor de bypass de serpentín típico $BF \approx 0.08$, equivalente a una humedad relativa efectiva de salida $HR_{inj} \approx 92\%$):
     $$P_{w,inj,k} = 0.92 \cdot P_{ws}(T_{inj,k}) = 0.92 \times 0.61078 \cdot \exp\left(\frac{17.27 \cdot T_{inj,k}}{T_{inj,k} + 237.3}\right) \quad [\text{kPa}]$$
     $$\omega_{inj,k} = \min\left(\omega_{ret}, \; 0.62198 \cdot \frac{P_{w,inj,k}}{P_{atm} - P_{w,inj,k}}\right) \quad \left[\text{kg}_{\text{vapor}} / \text{kg}_{\text{aire seco}}\right]$$
4. **Potencia Frigorífica Latente ($Q_{latente}$) y Caudal de Agua Condensada ($\dot{m}_{cond}$):**
   Utilizando la entalpía de vaporización del agua a la temperatura del serpentín ($h_{fg} \approx 2465\text{ kJ/kg}$):
   $$Q_{latente,k} = \dot{m}_{da,k} \cdot h_{fg} \cdot \max\left(0, \; \omega_{ret} - \omega_{inj,k}\right) \quad [\text{kW térmicos}]$$
   $$Q_{latente\_total} = \sum_{k \in U_{vent\_ON}} Q_{latente,k} \quad ; \quad \dot{m}_{cond} = \frac{Q_{latente\_total}}{h_{fg}} \times 3600 \quad [\text{Litros/hora de agua condensada}]$$
5. **Entalpía Específica por Par ($h_{ret,k}, h_{inj,k}$), Capacidad Total ($Q_{total}$) y Factor de Calor Sensible ($SHF$):**
   Dado que la sala cuenta con un único transmisor de humedad en la rejilla de retorno ($\omega_{ret}$ global), la entalpía específica de entrada y salida para cada par $k$ se formula explícitamente usando su temperatura local ($T_{ret,k}$, $T_{inj,k}$) y la humedad absoluta común de retorno ($\omega_{ret}$):
   $$h_{ret,k} = 1.006 \cdot T_{ret,k} + \omega_{ret} \cdot \left(2501 + 1.86 \cdot T_{ret,k}\right) \quad \left[\text{kJ/kg}_{da}\right]$$
   $$h_{inj,k} = 1.006 \cdot T_{inj,k} + \omega_{inj,k} \cdot \left(2501 + 1.86 \cdot T_{inj,k}\right) \quad \left[\text{kJ/kg}_{da}\right]$$
   La capacidad frigorífica total del sistema y el porcentaje de carga latente quedan dados por:
   $$Q_{total} = Q_{sensible\_total} + Q_{latente\_total} = \sum_{k \in U_{vent\_ON}} \dot{m}_{da,k} \cdot \left(h_{ret,k} - h_{inj,k}\right) \quad [\text{kW térmicos}]$$
   $$SHF = \frac{Q_{sensible\_total}}{Q_{total}} \quad ; \quad \% \text{Calor Latente} = (1 - SHF) \times 100 = \left(\frac{Q_{latente\_total}}{Q_{total}}\right) \times 100$$

---

### 4.5. Modelo Termodinámico de Ciclo de Carnot / Rankine Inverso y Eficiencia Exergética

En un ciclo de refrigeración por compresión de vapor (ciclo Rankine inverso), el trabajo eléctrico que debe realizar el compresor ($P_{elec\_total}$) depende directamente del **salto térmico de bombeo (*Temperature Lift*)** entre el foco frío (aire de retorno de la sala, $\overline{T}_{ret}$) y el foco caliente (aire ambiente exterior que enfría la unidad condensadora, $\overline{T}_{ext}$).

Con la incorporación de los $L = 2$ sensores de temperatura exterior, el **STM32H7** calcula en tiempo real tanto el rendimiento de Primera Ley ($COP$) como la eficiencia de Segunda Ley ($\eta_{II}$):

1. **Coeficiente de Rendimiento Operativo de Primera Ley ($COP_{real}$ y $COP_{sensible}$):**
   * **$COP_{real}$ (Total Entálpico de Máquina):** Evalúa la eficiencia total del ciclo de compresión de vapor:
     $$COP_{real} = \frac{Q_{total}}{P_{elec\_total}} = \frac{Q_{sensible\_total} + Q_{latente\_total}}{P_{elec\_total}}$$
   * **$COP_{sensible}$ (Útil para TI):** Evalúa cuántos kW de calor de servidores se remueven por cada kW eléctrico pagado:
     $$COP_{sensible} = \frac{Q_{sensible\_total}}{P_{elec\_total}}$$

2. **Temperaturas Absolutas de los Focos y de Cambio de Fase (Ciclo Rankine Inverso con Pinch Point):**
   En un sistema real aire-aire, para que exista transferencia de calor en los intercambiadores se requiere un diferencial finito de temperatura (*Pinch / Approach Temperature*) en el evaporador ($\Delta T_{pinch,evap} \approx 8\text{ K}$) y en el condensador ($\Delta T_{pinch,cond} \approx 10\text{ K}$):
   * **Temperatura Absoluta del Foco Frío (Aire de Retorno):** $T_{C,\text{aire}} = \overline{T}_{ret} + 273.15 \; [\text{K}]$
   * **Temperatura Absoluta del Foco Caliente (Aire Exterior):** $T_{H,\text{aire}} = \overline{T}_{ext} + 273.15 \; [\text{K}]$
   * **Temperaturas Absolutas Estimadas de Saturación del Refrigerante (Ciclo Interno Rankine Inverso):**
     $$T_{evap,\text{sat}} = (\overline{T}_{inj} - \Delta T_{pinch,evap}) + 273.15 \quad [\text{K}]$$
     $$T_{cond,\text{sat}} = (\overline{T}_{ext} + \Delta T_{pinch,cond}) + 273.15 \quad [\text{K}]$$

3. **Coeficiente de Rendimiento Ideal de Carnot ($COP_{Carnot}$):**
   Representa el límite máximo teórico fijado por la Segunda Ley de la Termodinámica para operar entre las temperaturas de cambio de fase del ciclo:
   $$COP_{Carnot} = \frac{T_{evap,\text{sat}}}{\max\left(5.0\text{ K}, \; T_{cond,\text{sat}} - T_{evap,\text{sat}}\right)}$$

4. **Eficiencia Exergética / de Segunda Ley ($\eta_{II}$ — Métrica Invariante al Clima Exterior):**
   La relación entre el $COP_{real}$ medido y el $COP_{Carnot}$ instantáneo define la **Eficiencia de Segunda Ley ($\eta_{II}$)**:
   $$\eta_{II} = \frac{COP_{real}}{COP_{Carnot}} \times 100 \quad [\%]$$
   * **Propiedad Fundamental para Mantenimiento Predictivo:** Para un compresor sano (tipo Scroll hermético en R410A), $\eta_{II}$ se mantiene estable en una banda típica del **$35\%$ al $50\%$** independientemente de si afuera hace $15\,^\circ\text{C}$ o $38\,^\circ\text{C}$. Si ocurre una **fuga de refrigerante**, **ensuciamiento del condensador exterior** o **desgaste mecánico del compresor**, $COP_{real}$ cae mientras $COP_{Carnot}$ permanece dictado por las temperaturas de los sensores, provocando una caída inmediata e inequívoca en $\eta_{II}$.

---

### 4.6. Modelo Matemático Reservado para Fase 2 (Con Sensores de Presión $4\text{–}20\text{ mA}$ — Cambio F-02)
*(Constancia de diseño para implementaciones futuras cuando se habiliten los sensores de presión en la configuración)*:

1. **Corrección Dinámica de Caudal con Sensor Tipo 2 ($\Delta P_{aire}$ de $0\text{ a }500\text{ Pa}$ en Filtros):**
   Cuando se instale el transmisor de presión diferencial en el filtro, el flujo másico $\dot{m}_{da}$ dejará de ser constante y se corregirá automáticamente ante la pérdida de carga del filtro ($\alpha \approx 0.35$):
   $$\dot{m}_{da,\text{corregido}} = \dot{m}_{da,\text{nominal}} \times \sqrt{\max\left(0.20, \; 1.0 - \alpha \cdot \frac{\Delta P_{aire} - \Delta P_{limpio}}{\Delta P_{max} - \Delta P_{limpio}}\right)}$$
2. **Termodinámica Exacta de R410A mediante Tabla de Búsqueda (LUT de 46 Puntos en Flash — Corrección F-02):**
   Cuando se instale el transmisor de presión manométrica ($P_{bar} \in [0.0, 45.0]\text{ bar}$) en la línea de succión o descarga del compresor, se descarta tanto el uso de polinomios cuadráticos como la evaluación en tiempo real de funciones trascendentes (`ln()` en ecuaciones de Antoine), protegiendo los ciclos de reloj del bucle de $50\text{ Hz}$ del Cortex-M7 y evitando errores de traslación de escala Kelvin/Celsius.
   * **Implementación por LUT Estática e Interpolación Lineal $\mathcal{O}(1)$:** El firmware almacena en memoria Flash (`const float R410A_TSAT_LUT[46]`) los **46 valores exactos de temperatura de saturación de punto de rocío ($T_{sat}$ en $^\circ\text{C}$, según tablas NIST REFPROP para R410A)** espaciados cada $1.0\text{ bar}$ de presión manométrica (desde $0\text{ bar}$ hasta $45\text{ bar}$).
   * Para cualquier presión medida $P_{bar} \in [0.0, 45.0)$, se obtiene el índice entero $i = \lfloor P_{bar} \rfloor$ y la fracción residual $f = P_{bar} - i$, calculando la temperatura de saturación y el sobrecalentamiento útil ($\Delta T_{SH}$) en apenas tres instrucciones de la FPU:
     $$T_{sat}\,[{}^\circ\text{C}] = \text{LUT}[i] + f \cdot \left(\text{LUT}[i+1] - \text{LUT}[i]\right) \quad ; \quad \Delta T_{SH} = T_{succion} - T_{sat}$$

---

## 5. ANALÍTICA PREDICTIVA Y MATRIZ DE DIAGNÓSTICOS

### 5.1. Firma Térmica Base (*Baseline*) mediante OLS en Doble Precisión (`double` 64 bits — Cambio F-03)
Durante los primeros **7 días (168 horas)** de operación inicial en régimen estable (`STEADY_STATE`), el **STM32H7** entrena mediante **Mínimos Cuadrados Ordinarios (OLS)** el modelo predictivo de la **Capacidad Frigorífica Total Esperada ($\widehat{Q}_{total,esp}$)** en función de tres variables exógenas al consumo eléctrico: la temperatura de retorno ($\overline{T}_{ret}$), la temperatura exterior ($\overline{T}_{ext}$) y la **fracción de capacidad de compresores encendidos ($F_{carga} \in (0, 1]$)**:

$$\widehat{Q}_{total,esp} = \beta_0 + \beta_1 \cdot \overline{T}_{ret} + \beta_2 \cdot \overline{T}_{ext} + \beta_3 \cdot F_{carga} \quad [\text{kW térmicos}]$$

#### Requisito de Precisión Numérica en Hardware (Prevención de Mal Condicionamiento — F-03):
Dado que en una sala crítica de telecomunicaciones la temperatura de retorno $\overline{T}_{ret}$ permanece regulada dentro de una banda estrecha (p. ej., $22\,^\circ\text{C}$ a $25\,^\circ\text{C}$), las columnas de la matriz de diseño $X$ presentan alta colinealidad con el término independiente ($\beta_0$), haciendo que el determinante de la matriz normal $X^T X$ sea cercano a cero. Si la inversión se ejecutara en precisión simple (`float` de 32 bits, 24 bits de mantisa), el error de truncamiento corrompería los coeficientes $\beta$.
1. **Aritmética Obligatoria en 64 bits (`double` en DP-FPU):** La acumulación de las matrices normales ($X^T X \in \mathbb{R}^{4 \times 4}$ y $X^T Y \in \mathbb{R}^{4 \times 1}$) y su resolución algebraica (mediante descomposición de Cholesky / Gauss-Jordan) deben ejecutarse **estrictamente en punto flotante de doble precisión IEEE 754 (`double` de 64 bits, 53 bits de mantisa)** utilizando las instrucciones nativas por hardware de la **Unidad de Punto Flotante de Doble Precisión (DP-FPU)** del procesador ARM Cortex-M7 (`STM32H7`).
2. **Regularización Diagonal de Tikhonov (*Ridge Protection*):** Antes de invertir la matriz normal en 64 bits, se adiciona un término de regularización infinitesimal en la diagonal principal ($\lambda = 10^{-6}$) para garantizar invertibilidad matemática absoluta incluso si la temperatura de la sala o la fracción de carga permanecieran 100% invariantes durante los 7 días:
   $$\beta = \left(X^T X + \lambda I_4\right)^{-1} X^T Y$$

Una vez estimado el numerador térmico teórico para las condiciones actuales, el **COP Esperado ($\widehat{COP}_{esperado}$)** y el porcentaje de degradación mecánica se calculan como:

$$\widehat{COP}_{esperado} = \frac{\widehat{Q}_{total,esp}}{P_{elec\_total}}$$

$$\% \text{Degradación Mecánica} = \text{clamp}_{0..100}\left( \max\left( \frac{\widehat{COP}_{esperado} - COP_{real}}{\widehat{COP}_{esperado}}, \; \frac{\eta_{II,\text{base}} - \eta_{II}}{\eta_{II,\text{base}}} \right) \times 100 \right)$$

---

### 5.2. Reglas de Diagnóstico Predictivo para Fase 1 (Basadas en Matriz $N+N+M+L$, $HR_{ret}$ y Energía)

1. **Predicción de Obstrucción de Filtros y Jerarquía de Inicialización en Días Restantes (Cambio F-04):**
   * **Física:** Al obstruirse un filtro, el caudal de aire cae drásticamente; el aire permanece más tiempo en contacto con el serpentín frío, provocando que $T_{inj,k}$ descienda anormalmente hacia el umbral de congelamiento ($9.0\,^\circ\text{C}$) mientras $\Delta T_k$ aumenta por encima de su valor base ($\Delta T_{base,k}$).
   * **Índice de Probabilidad ($0\text{ a }100\%$):**
     $$\% \text{Filtro}_k = \text{clamp}_{0..100}\left( 60 \cdot \frac{T_{inj\_base,k} - T_{inj,k}}{3.0^\circ\text{C}} + 40 \cdot \frac{\Delta T_k - \Delta T_{base,k}}{4.0^\circ\text{C}} \right)$$
   * **Regla de Precedencia y Bloqueo de Inicialización para "Días Restantes" ($\text{Días Restantes}_k$ — Corrección F-04):**
     Para eliminar cualquier ambigüedad en los registros publicados hacia **Modbus TCP**, **SNMP** y **API REST** durante el arranque del equipo o con filtros limpios, el firmware evalúa la siguiente estructura condicional en orden de prioridad estricta:
     1. **Condición 1 — Periodo de Inicialización ($N_{horas\_RAM} < 168\text{ horas}$):** Hasta que no se acumulen las primeras **168 horas (7 días)** de historia en el *Ring Buffer* de la RAM, el sistema no dispone de base estadística para calcular derivadas diarias. La variable $\text{Días Restantes}_k$ se mantiene bloqueada en el **código de inicialización**: valor numérico `100` (días) para visualización HMI/JSON y código de registro `0xFFFF` en el mapa crudo de diagnóstico, evitando proyecciones ficticias en el Día 1.
     2. **Condición 2 — Filtro Limpio o Sin Tendencia Descendente ($\% \text{Filtro}_k < 25\%$ ó $m_{T,k} \ge -0.05\,^\circ\text{C/día}$):** Si ya se cumplieron las 168 horas pero el índice de saturación es menor al $25\%$ o la temperatura de inyección no presenta pendiente negativa real, la variable reporta el **tope operativo sano fijo de `90` días**.
     3. **Condición 3 — Degradación Activa en Curso ($N_{horas\_RAM} \ge 168$, $\% \text{Filtro}_k \ge 25\%$ y $m_{T,k} < -0.05\,^\circ\text{C/día}$):** Se aplica la proyección diferencial acotada hacia el umbral de congelamiento ($7.5\,^\circ\text{C}$):
        $$\text{Días Restantes}_k = \min\left(90.0, \; \frac{\max\left(0.0, \; T_{inj,k} - 7.5^\circ\text{C}\right)}{\max\left(|m_{T,k}|, \; 0.05^\circ\text{C/día}\right)}\right)$$

2. **Predicción de Fuga de Refrigerante, Condensador Sucio o Fatiga de Compresor (Desacoplado de $\overline{T}_{ext}$):**
   * **Física:** Gracias a los sensores exteriores ($\overline{T}_{ext}$), el algoritmo distingue entre dos fenómenos:
     * **Aumento Normal de Consumo por Clima Exterior Cálido:** Si $P_{elec\_total}$ sube y $\Delta T_k$ baja ligeramente porque $\overline{T}_{ext}$ subió a $> 32\,^\circ\text{C}$, la eficiencia de Segunda Ley ($\eta_{II}$) y la capacidad esperada $\widehat{Q}_{total,esp}(\overline{T}_{ext})$ acompañan el cambio climático $\implies$ **Cero falsas alarmas**.
     * **Fuga Real de Refrigerante o Falla en Unidad:** La eficiencia exergética $\eta_{II}$ cae más de un $20\%$ respecto a $\eta_{II,\text{base}}$, el diferencial $\Delta T_k$ cae por debajo de $\Delta T_{base,k} \cdot F_{carga}$ y $T_{inj,k}$ se vuelve tibia ($> 15\,^\circ\text{C}$) a pesar de que $\overline{T}_{ext}$ es moderada.
   * **Índice de Probabilidad ($0\text{ a }100\%$):**
     $$\% \text{Fuga}_k = \text{clamp}_{0..100}\left( 50 \cdot \frac{(\Delta T_{base,k} \cdot F_{carga}) - \Delta T_k}{0.5 \cdot (\Delta T_{base,k} \cdot F_{carga})} + 50 \cdot \frac{\eta_{II,\text{base}} - \eta_{II}}{0.35 \cdot \eta_{II,\text{base}}} \right) \quad (\text{si } \% \text{Filtro}_k < 50\%)$$

3. **Detección Generalizada de Punto Caliente (*Hotspot*) o Cortocircuito de Aire en Sala (Para cualquier $M$):**
   * **Asimetría Térmica Espacial en Sala:** Se activa para cualquier cantidad $M \ge 2$ de sensores ambientales cuando la dispersión entre el punto más caliente y el más frío de la sala supera el umbral de homogeneidad:
     $$\Delta T_{sala,max} = \max_{m \in \{1..M\}}\left(T_{sala,m}\right) - \min_{m \in \{1..M\}}\left(T_{sala,m}\right) > 3.5\,^\circ\text{C}$$
   * **Cortocircuito de Flujo de Aire (*Bypass*):** Se activa si $T_{sala,max} > 27.0\,^\circ\text{C}$ o si $\Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret} > 3.0\,^\circ\text{C}$ (el aire frío inyectado retorna directamente a las máquinas sin barrer el calor de los racks).

4. **Detección de Pérdida de Hermeticidad / Infiltración de Humedad Exterior (Usando $HR_{ret}$ en Rejilla):**
   * Se dispara cuando el porcentaje de esfuerzo latente medido en el retorno supera el umbral ($\text{\% Calor Latente} > 30\%$ o $SHF < 0.70$) o cuando la humedad absoluta en la rejilla de retorno supera $\omega_{ret} > 0.012\text{ kg}_{v}/\text{kg}_{da}$ ($HR_{ret} > 65\%$ a $24\,^\circ\text{C}$) de forma sostenida.

5. **Predicción de Desgaste Electromecánico por Ciclos Cortos:**
   * Se activa cuando el conteo de re-arranques con $t_{off} < 180\text{ s}$ de cualquier compresor supera 3 eventos en una misma hora.

### 5.3. Ampliación de Diagnóstico Reservada para Fase 2 (Con Sensores de Presión $4\text{–}20\text{ mA}$)
*(Constancia funcional para siguientes etapas)*:
* Al habilitar el **Sensor Tipo 2 ($\Delta P_{aire}$)**, el porcentaje de obstrucción de filtro pasará a medirse directamente por diferencia de presiones: $\% \text{Filtro} = \left(\frac{\Delta P_{aire} - \Delta P_{limpio}}{\Delta P_{max} - \Delta P_{limpio}}\right) \times 100$.
* Al habilitar el **Sensor Tipo 1 ($P_{gas}$)** evaluado mediante la LUT de 46 puntos en Flash, se separará con certeza absoluta una **fuga de refrigerante** ($P_{bar} < 6.5\text{ bar}$ con $\eta_{II}$ baja) de un **condensador exterior sucio** ($P_{bar}$ de alta elevada con $\overline{T}_{ext}$ normal) o una **falla de válvulas del compresor** ($P_{bar}$ de succión $> 11.5\text{ bar}$ con contactor cerrado).

---

### 5.4. Auditoría de Capacidad y Reserva Frigorífica Libre Corregida por Clima Exterior ($kW_{libres}$)
La capacidad frigorífica nominal de placa de un equipo de aire acondicionado ($Q_{nominal,u}$) está especificada para una temperatura exterior estándar de diseño ($T_{ext,dise\tilde{n}o}$, típicamente $35.0\,^\circ\text{C}$ según norma AHRI 210/240). Cuando la temperatura exterior real ($\overline{T}_{ext}$) supera $T_{ext,dise\tilde{n}o}$, la presión de condensación sube y la capacidad máxima física que puede entregar la máquina disminuye (*Thermal Derating*).

1. **Capacidad Nominal Corregida por Clima Exterior ($Q_{nom,corr,u}$):**
   Aplicando el coeficiente de penalización térmica por condensación elevada ($k_{ext} \approx 0.005\,^\circ\text{C}^{-1}$, equivalente a un $-0.5\%$ de capacidad por cada $1\,^\circ\text{C}$ por encima de $T_{ext,dise\tilde{n}o}$):
   $$Q_{nom,corr,u} = Q_{nominal,u} \times \left[ 1.0 - k_{ext} \cdot \max\left(0.0, \; \overline{T}_{ext} - T_{ext,dise\tilde{n}o}\right) \right] \quad [\text{kW térmicos}]$$
2. **Capacidad Máxima Segura en Redundancia ($Q_{max\_seguro}$) y Margen Libre ($kW_{libres}$):**
   Para una sala donde se exige mantener una redundancia operativa (máximo $U_{req}$ unidades necesarias para cubrir la carga sin comprometer la unidad de respaldo en un arreglo $2+1$), descontando tanto el *derating* climático como la degradación mecánica acumulada:
   $$Q_{max\_seguro} = \left(\sum_{u=1}^{U_{req}} Q_{nom,corr,u}\right) \times \left(1.0 - \frac{\% \text{Degradación Mecánica}}{100.0}\right)$$
   $$kW_{libres} = \max\left(0.0, \; Q_{max\_seguro} - Q_{sensible\_total}\right) \quad [\text{kW térmicos}]$$

---

## 6. ARQUITECTURA DE DATOS Y ALMACENAMIENTO HÍBRIDO (RAM + EEPROM + MICROSD)

El sistema evita el desgaste prematuro de la tarjeta MicroSD y maximiza la velocidad de cálculo repartiendo los datos en tres memorias según su naturaleza temporal:

### 6.1. Nivel 1: Memoria RAM Interna del STM32H7 (Cálculo en Vivo y Ventana de 30 Días)
* **Acumuladores de Tiempo Real (Segundo a Segundo):** Todas las lecturas de los $2N + M + L$ sensores de temperatura (**16 sensores en el piloto: $6+6+2+2$**), la humedad $4\text{–}20\text{ mA}$ en rejilla de retorno ($HR_{ret}$) y la energía se filtran y promedian cada segundo exclusivamente en la memoria RAM.
* **Buffer Circular Histórico (*Ring Buffer* de 720 Horas = 30 Días):** La RAM conserva de forma permanente los últimos **720 resúmenes horarios**. Con un tamaño estructurado de **64 bytes por registro horario**, los 30 días completos ocupan únicamente **$46.08\text{ KB}$** del megabyte de RAM disponible en el STM32H7.
* **Cero Latencia y Álgebra de 64 bits:** Las regresiones matriciales OLS (acumuladas e invertidas en precisión `double` de 64 bits sobre la DP-FPU del STM32H7), el cálculo de pendientes diarias acotadas y las consultas de la API REST de hasta 30 días se ejecutan leyendo directamente la RAM en microsegundos.

### 6.2. Nivel 2: Memoria EEPROM $\text{I}^2\text{C}$ (`AT24C256C` de 32 KB — Configuración y Firma Base)
Resguarda de forma inmutable:
1. **Parámetros de Topología del Sitio:** Número de pares configurados ($N=6$ en el piloto), sensores de sala ($M=2$), sensores exteriores ($L=2$), temperatura exterior de diseño ($T_{ext,dise\tilde{n}o} = 35.0\,^\circ\text{C}$), coeficiente $k_{ext}$, mapeo de direcciones Modbus del módulo `CWT-TM-16PT`, mapeo de entradas digitales de ventiladores ($U_{vent\_ON}$) y compresores, y altitud del local ($Z$).
2. **Calibración y Firma Térmica del Día 1 (*Baseline*):** Coeficientes $\beta_0, \beta_1, \beta_2, \beta_3$ almacenados en formato `double` (64 bits, 32 bytes en total) del modelo OLS de capacidad térmica, Eficiencia Exergética Base ($\eta_{II,\text{base}}$), diferenciales iniciales por par ($\Delta T_{base,1..N}$, $T_{inj\_base,1..N}$) y el consumo eléctrico aprendido por cada compresor ($\Delta P_{comp,c}$).

### 6.3. Nivel 3: Tarjeta MicroSD Industrial (Datalogger Binario Indexado de Largo Plazo)
* **Estructura del Bloque Horario en MicroSD (64 bytes fijos por hora):**
  * `Timestamp UNIX` (4 bytes)
  * `Vector de Temperaturas de Inyección Promedio` ($N=6$ valores en décimas de $^\circ\text{C}$, con capacidad hasta 7 pares: 14 bytes)
  * `Vector de Temperaturas de Retorno Promedio` ($N=6$ valores en décimas de $^\circ\text{C}$, con capacidad hasta 7 pares: 14 bytes)
  * `Vector de Temperaturas de Sala Promedio` ($M=2$ valores en décimas de $^\circ\text{C}$, hasta 2 sensores: 4 bytes)
  * `Vector de Temperaturas Exteriores Promedio` ($L=2$ valores en décimas de $^\circ\text{C}$, hasta 2 sensores: 4 bytes)
  * `Humedad Relativa en Retorno Promedio` ($HR_{ret}$ en décimas de $\%HR$: 2 bytes)
  * `Potencia Eléctrica Activa Promedio` (4 bytes - Float $\text{kW}$)
  * `Capacidad Térmica Sensible y Latente Total` (8 bytes - 2x Float $\text{kW}$)
  * `COP Real Promedio y Eficiencia de Carnot (Eta_II)` (4 bytes - 2x UINT16 en centésimas)
  * `Máscara de Estado de Ventiladores (U_vent_ON), Compresores, Fracción de Carga y Conteo de Arranques` (4 bytes)
  * `Checksum de Integridad CRC-16` (2 bytes)
  * **Total Exacto:** **64 bytes por hora** ($560.6\text{ KB}$ por año de operación continua).
* **Reconstrucción Instantánea al Arranque (*Boot Restore*):** Ante un corte eléctrico total en la estación, al volver la energía el STM32H7 lee los últimos $46.08\text{ KB}$ de la MicroSD, verifica el contador de horas válidas ($N_{horas\_RAM}$) y restaura en menos de $10\text{ ms}$ los 30 días de historia en la memoria RAM sin reiniciar innecesariamente el bloqueo de 168 horas si la historia ya existía en la SD.

### 6.4. Requisito de Plataforma: Repartición de Flash para la Imagen con Red
La imagen de aplicación que integra la pila de red (Ethernet/WiFi, sockets, SNMP, HTTP y TLS) supera el tamaño del slot actual de $256\text{ KB}$. La arquitectura OTA del proyecto (`doc/architecture/mcuboot_external_flash_ota_architecture.md`) ya define la repartición asimétrica prevista: slot 0 en flash interna, slot 1 en la QSPI externa ($1.75\text{ MB}$) y una partición LittleFS de $256\text{ KB}$.

Esta repartición es un **prerrequisito de la fase de interfaces**, no un cambio de la presente especificación. El DTS no se modifica en esta revisión.

---

## 7. INTERFACES DE COMUNICACIÓN Y VISUALIZACIÓN

1. **Pantalla Táctil Local (HMI DWIN 4.3"):**
   * Presenta en la vista principal los promedios globales ($\overline{T}_{ret}$, $\overline{T}_{inj}$, $\overline{\Delta T}_{clima}$, $\overline{T}_{sala}$, $\overline{T}_{ext}$), Humedad en Retorno ($HR_{ret}$), Potencia Eléctrica ($\text{kW}$), Capacidad Frigorífica Sensible y Latente ($\text{kW}$), $COP_{real}$, Eficiencia de Carnot ($\eta_{II}$), Reserva Libre corregida ($kW_{libres}$) y alertas predictivas activas, contando con una sub-pantalla de detalle para inspeccionar individualmente los **$6$ pares de Inyección/Retorno ($T_{inj,1..6}$, $T_{ret,1..6}$, $\Delta T_{1..6}$)**, los **$2$ sensores de sala ($T_{sala,1..2}$)** y los **$2$ sensores exteriores ($T_{ext,1..2}$)**.
2. **Modbus TCP Server (Integración con Huawei NetEco / SCADA):**
   * El controlador opera como **Modbus TCP Server** (Puerto 502) sobre la interfaz Ethernet integrada, soportando múltiples conexiones simultáneas de clientes/maestros. Mapea en bloques contiguos de *Holding Registers* (`0x03`) e *Input Registers* (`0x04`) los valores globales de la sala, las **16 temperaturas del esquema $(6+6+2+2)$**, la humedad en retorno ($4\text{–}20\text{ mA}$), los parámetros del analizador de energía, el estado de ventiladores ($U_{vent\_ON}$) y compresores, el $COP_{real}$, el $COP_{Carnot}$, la eficiencia exergética ($\eta_{II}$), el margen de reserva libre corregido por clima ($kW_{libres}$), el porcentaje de degradación mecánica, los días restantes para mantenimiento (con soporte del código de inicialización `0xFFFF` / `100`) y la matriz de códigos de alerta predictiva.
3. **Agente SNMP (Simple Network Management Protocol v2c / v3):**
   * Expone una **MIB (Management Information Base)** estructurada con los OIDs correspondientes a las variables termodinámicas, psicrométricas, eléctricas ($P_{elec\_total}$, $COP_{real}$, $COP_{Carnot}$, $\eta_{II}$, $kW_{libres}$), lecturas de los 16 sensores de temperatura ($6+6+2+2$) e indicadores de degradación para consulta directa (*SNMP GET / WALK* en puerto UDP 161) desde gestores de red corporativos (NMS como Zabbix, PRTG, SolarWinds o sistemas OSS de Entel).
   * Incluye emisión asíncrona de **SNMP Traps / Informs** (puerto UDP 162) para notificar de forma inmediata la activación o despeje de alertas predictivas (obstrucción de filtros, posible fuga de refrigerante, puntos calientes en sala, pérdida de aislamiento o ciclos cortos).
4. **API RESTful Local (HTTP/JSON):**
   * Expone los endpoints `GET /api/v1/hvac/status` (con el desglose de `pares_termicos[1..6]`, `sensores_sala[1..M]`, `sensores_exterior[1..L]`, telemetría termodinámica/Carnot, psicrometría de retorno y analítica), `GET /api/v1/hvac/logs?hours=X` y `POST /api/v1/hvac/config`, protegidos mediante autenticación *Bearer Token* y lista blanca de direcciones IP (*IP Whitelisting*).
