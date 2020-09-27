package th.bpmon.bluetooth_bp_monitor;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Build;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;

import org.florescu.android.rangeseekbar.RangeSeekBar;

import androidx.annotation.RequiresApi;
import androidx.appcompat.app.AppCompatDialogFragment;
import androidx.fragment.app.FragmentManager;

public class OptionsDialog extends AppCompatDialogFragment {
    private SeekBar timerFrequency;
    private SeekBar numReadings;
    private TextView timeUpdate;
    private TextView readUpdate;

    private int timerFreq;
    private int timerNumReadings;

    private OptionsDialogListener listenerOpt;

    @Override
    public void onAttach(Context context) {
        super.onAttach(context);

        try {
            listenerOpt = (OptionsDialogListener) getTargetFragment();
        } catch (ClassCastException e) {
            throw new ClassCastException(context.toString() +
                    "must implement OptionsDialogListener");
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        AlertDialog.Builder builder = new AlertDialog.Builder(getActivity());

        LayoutInflater inflater = getActivity().getLayoutInflater();
        View view = inflater.inflate(R.layout.options_dialog, null);

        timerFreq = getArguments().getInt("timerFreq");
        timerNumReadings = getArguments().getInt("timerNumReadings");


        builder.setView(view)
                .setTitle("Timer Options")
                .setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {

                    }
                })
                .setPositiveButton("Set Options", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        int timerFreq = (int) timerFrequency.getProgress();
                        int timerNum = (int) numReadings.getProgress();

                        listenerOpt.applyOptions(timerFreq, timerNum);
                    }
                });

        timerFrequency = view.findViewById(R.id.timerFrequency);
        numReadings = view.findViewById(R.id.timerNum);

        timerFrequency.incrementProgressBy(10);
        timerFrequency.setMax(360);
        numReadings.setMax(50);

        timerFrequency.setProgress(timerFreq);
        numReadings.setProgress(timerNumReadings);


        timeUpdate = view.findViewById(R.id.timeUpdate);
        readUpdate = view.findViewById(R.id.readUpdate);

        timeUpdate.setText("" + timerFreq);
        readUpdate.setText("" + timerNumReadings);

        timerFrequency.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {

            @Override
            public void onProgressChanged(SeekBar seekBar, int i, boolean b) {
                timeUpdate.setText(String.valueOf(i));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {

            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {

            }
        });

        numReadings.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {

            @Override
            public void onProgressChanged(SeekBar seekBar, int i, boolean b) {
                readUpdate.setText(String.valueOf(i));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {

            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {

            }
        });


        return builder.create();
    }

    @Override
    public void show(FragmentManager manager, String tag) {
        super.show(manager, tag);
    }

    public interface OptionsDialogListener{
        void applyOptions(int timerFreq, int timerNum);
    }


}
