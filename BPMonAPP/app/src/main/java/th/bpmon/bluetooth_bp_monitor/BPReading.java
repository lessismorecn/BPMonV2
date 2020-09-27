package th.bpmon.bluetooth_bp_monitor;

import java.util.Date;

import androidx.annotation.NonNull;

public class BPReading {
    Date date;
    float MAP;
    float systolic;
    float diastolic;

     public BPReading(Date date, float MAP, float systolic, float diastolic) {
         this.date = date;
         this.MAP = MAP;
         this.systolic = systolic;
         this.diastolic = diastolic;
     }

    @NonNull
    @Override
    public String toString() {
        String ret = "";

        long millis = this.date.getTime();

        millis = millis/1000;

        ret += Long.toString(millis);
        ret += "/";
        ret += Float.toString(this.MAP);
        ret += "/";
        ret += Float.toString(this.systolic);
        ret += "/";
        ret += Float.toString(this.diastolic);
        ret += "/";

        return(ret);

    }

    public String toString2(){
        String ret = "";


        ret += this.date.toString();
        ret += "\n";
        ret += " MAP: ";
        ret += Float.toString(this.MAP);
        ret += " Systolic/Diastolic: ";
        ret += Float.toString(this.systolic);
        ret += "/";
        ret += Float.toString(this.diastolic);
        ret += "\n";

        return(ret);
    }
}
