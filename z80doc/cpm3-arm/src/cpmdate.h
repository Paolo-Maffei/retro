// see simh/dosplus/FDATE.MAC
//
// PROCEDURE drtodate(thedate : integer; VAR yr, mo, day : integer);
// (* 1 Jan 1978 corresponds to Digital Research date = 1  *)
// (* BUG - cannot handle negative values for dates > 2067 *)
//
//   VAR
//     i, y1        : integer;
//     dayspermonth : ARRAY[1..12] OF 1..31;
//
//   BEGIN (* drtodate *)
//   FOR i := 1 TO 12 DO dayspermonth[i] := 31;
//   dayspermonth[4] := 30; dayspermonth[6] := 30;
//   dayspermonth[9] := 30; dayspermonth[11] := 30;
//   IF thedate > 731 THEN BEGIN (* avoid overflows *)
//     yr := 1980; thedate := thedate - 731; END
//   ELSE BEGIN
//     thedate := thedate + 730; yr := 1976; END;
//   (* 0..365=y0; 366..730=y1; 731..1095=y2; 1096..1460=y3 *)
//   i := thedate DIV 1461; thedate := thedate MOD 1461;
//   y1 := (thedate-1) DIV 365; yr := yr + y1 + 4*i;
//   IF y1 = 0 THEN (* leap year *) dayspermonth[2] := 29
//   ELSE BEGIN
//     thedate := thedate - 1; (* 366 -> 365 -> 1 Jan *)
//     dayspermonth[2] := 28; END;
//   day := thedate - 365*y1 + 1; mo := 1;
//   WHILE day > dayspermonth[mo] DO BEGIN
//     day := day - dayspermonth[mo];
//     mo := succ(mo); END;
//   END; (* drtodate *)
//
// Incorporate (a) in year (c), overflows to century (b)

void dr2date (int date, RTC::DateTime* dt) {
    static uint8_t daysInMonth [] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 
    };

    int yr;
    if (date >731) {
        date -= 731;
        yr = 1980;
    } else {
        date += 730;
        yr = 1976;
    }
    int i = date / 1461;
    date %= 1461;
    int y1 = (date-1) / 365;
    yr += y1 + 4*i;
    if (y1 == 0)
        daysInMonth[1] = 29;
    else {
        --date;
        daysInMonth[1] = 28;
    }
    int day = date - 365*y1 + 1;
    int mon = 0;
    while (day > daysInMonth[mon]) {
        day -= daysInMonth[mon];
        ++mon;
    }
    dt->dy = day;
    dt->mo = mon+1;
    dt->yr = yr%100;
    //printf("dr2date: y %d m %d d %d\n", yr, mon+1, day);
}

// see https://www.oryx-embedded.com/doc/date__time_8c_source.html

uint16_t date2dr (int y, int m, int d) {
   // count Jan and Feb as months 13 and 14 of the previous year
   if(m <= 2) {
      m += 12;
      --y;
   }
   // this should work for at least y = 1..63 (i.e. 2001..2063)
   return 365*y + y/4 - y/100 + y/400 + 30*m + (3*(m+1))/5 + d + 8003;
}
