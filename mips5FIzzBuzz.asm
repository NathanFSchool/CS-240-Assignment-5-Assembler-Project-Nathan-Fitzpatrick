.data
fizz: .asciiz "Fizz"
buzz: .asciiz "Buzz"

.text

main:

	addi $t0, $zero, 9
	
	addi $t1, $zero, 3
	addi $t2, $zero, 5
	

	div $t0, $t1
	mfhi $t3
	

	beq $t3, $zero, third
	
check_five:	


	div $t0, $t2
	mfhi $t3
	


	beq $t3, $zero, fifth
	
	j end
	
third:
	
	addi $v0, $zero, 4
	
	lui   $a0, 0x1001
    	ori   $a0, $a0, 0x0000
	
	syscall
	
	j check_five
	
fifth:
	addi $v0, $zero, 4
	
	lui   $a0, 0x1001
    	ori   $a0, $a0, 0x0005
	
	syscall
	
end:


	addi $v0, $zero, 10
	syscall
